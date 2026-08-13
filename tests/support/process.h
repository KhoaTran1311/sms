#pragma once

// P0-14: minimal POSIX subprocess helpers for the end-to-end integration
// tests (spawn real binaries — serverd, sms-cli — as children, redirect
// their stdio to files, feed one a pipe, reap them deterministically).
//
// Ownership contract:
//   * every child spawned here is reaped by waitpid before any helper
//     returns — Spawn reaps exec-failures, WaitPid/Terminate/Kill always
//     reap on the happy path, and Terminate escalates TERM -> KILL so a
//     stuck child can never leak a zombie from a fixture teardown;
//   * stdout/stderr are redirected to files (or /dev/null) so a daemon
//     under test can never corrupt the harness's own console output;
//   * no std::this_thread::sleep_for anywhere: waits use poll(2) timeouts
//     (event-driven, same budget as the deadline timers used in test logic).

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace sms::test {
namespace process {

struct SpawnOptions {
  // Child working directory; empty = inherit the parent's.
  std::filesystem::path cwd;
  // Child stdout/stderr are redirected to these files (created/truncated).
  // When empty the corresponding stream is sent to /dev/null — a child
  // under test must never share the test harness's console.
  std::filesystem::path stdout_file;
  std::filesystem::path stderr_file;
  // When true the child's stdin is a pipe; the parent receives the write
  // end in SpawnResult::stdin_fd (caller closes it to send EOF).
  bool pipe_stdin = false;
};

struct SpawnResult {
  pid_t pid = -1;
  int stdin_fd = -1;  // valid only when SpawnOptions::pipe_stdin
  std::string error;  // non-empty on failure (child was reaped if forked)
};

struct WaitResult {
  bool reaped = false;    // waitpid returned the child (no zombie remains)
  bool exited = false;    // WIFEXITED
  bool signaled = false;  // WIFSIGNALED
  int exit_code = -1;     // valid when exited
  int term_signal = -1;   // valid when signaled
  bool escalated = false; // a signal (TERM/KILL) had to be sent before exit
  std::string error;      // non-empty on waitpid errors (e.g. ECHILD)
};

// Spawns `executable` with `args` (argv[0] is the executable's filename;
// do not pass it yourself). Redirects stdout/stderr per `opts`, optionally
// pipes stdin. A forked child that fails early (chdir, open, dup2, exec)
// reports errno through an internal error pipe and is reaped before Spawn
// returns; on success the parent returns immediately with the live pid.
inline SpawnResult Spawn(const std::filesystem::path& executable,
                         const std::vector<std::string>& args,
                         const SpawnOptions& opts = {}) {
  SpawnResult result;

  if (!std::filesystem::exists(executable)) {
    result.error = "executable not found: " + executable.string();
    return result;
  }

  // Child -> parent failure-report pipe: a single ExecFailure struct, or
  // EOF once execv has replaced the child (closing the write end).
  enum class FailStage : std::uint8_t {
    kChdir, kOpenStdout, kOpenStderr, kDupStdin, kDupStdout, kDupStderr, kExec
  };
  struct ExecFailure {
    FailStage stage;
    int errno_value;
  };

  int err_pipe[2] = {-1, -1};
  int stdin_pipe[2] = {-1, -1};
  if (pipe(err_pipe) != 0) {
    result.error = std::string("pipe(err_pipe): ") + std::strerror(errno);
    return result;
  }
  if (opts.pipe_stdin && pipe(stdin_pipe) != 0) {
    close(err_pipe[0]);
    close(err_pipe[1]);
    result.error = std::string("pipe(stdin_pipe): ") + std::strerror(errno);
    return result;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    const int saved = errno;
    close(err_pipe[0]);
    close(err_pipe[1]);
    if (stdin_pipe[0] != -1) {
      close(stdin_pipe[0]);
      close(stdin_pipe[1]);
    }
    result.error = std::string("fork: ") + std::strerror(saved);
    return result;
  }

  if (pid == 0) {  // ---- child ----
    // CLOEXEC on the pipe ends we keep open: they must die with execv, or
    // the exec'd program would inherit the write end and the parent's
    // blocking read on the error pipe would never see EOF. (FD_CLOEXEC only
    // takes effect at exec, so a pre-exec failure can still report errno.)
    fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);
    close(err_pipe[0]);
    if (stdin_pipe[0] != -1) {
      fcntl(stdin_pipe[0], F_SETFD, FD_CLOEXEC);
      close(stdin_pipe[1]);  // parent keeps the write end
    }

    auto fail = [&](FailStage stage) {
      const ExecFailure f{stage, errno};
      ssize_t ignored = write(err_pipe[1], &f, sizeof(f));
      (void)ignored;
      _exit(127);
    };

    if (!opts.cwd.empty() && chdir(opts.cwd.c_str()) != 0) fail(FailStage::kChdir);
    if (stdin_pipe[0] != -1) {
      if (dup2(stdin_pipe[0], STDIN_FILENO) < 0) fail(FailStage::kDupStdin);
      close(stdin_pipe[0]);
    }
    const int out_fd = open(opts.stdout_file.c_str(),
                            O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) fail(FailStage::kOpenStdout);
    if (dup2(out_fd, STDOUT_FILENO) < 0) fail(FailStage::kDupStdout);
    close(out_fd);
    const int err_fd = open(opts.stderr_file.c_str(),
                            O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (err_fd < 0) fail(FailStage::kOpenStderr);
    if (dup2(err_fd, STDERR_FILENO) < 0) fail(FailStage::kDupStderr);
    close(err_fd);

    // argv[0] = executable filename, then the caller's args, then NULL.
    const std::string argv0 = executable.filename().string();
    std::vector<const char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(argv0.c_str());
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    execv(executable.c_str(), const_cast<char* const*>(argv.data()));
    fail(FailStage::kExec);
    _exit(127);  // unreachable; appeases -Wunreachable-code on some toolchains
  }

  // ---- parent ----
  close(err_pipe[1]);
  result.pid = pid;
  if (stdin_pipe[1] != -1) {
    result.stdin_fd = stdin_pipe[1];
    close(stdin_pipe[0]);
    fcntl(result.stdin_fd, F_SETFD, FD_CLOEXEC);  // safety: never leak to exec
  }

  // Blocking read on the error pipe: returns 0 (EOF) the moment execv has
  // run, or one ExecFailure struct if the child could not start. Exec always
  // closes the write end, so this cannot hang on a live child.
  ExecFailure failure{};
  ssize_t n = read(err_pipe[0], &failure, sizeof(failure));
  if (n < 0 && errno == EINTR) n = read(err_pipe[0], &failure, sizeof(failure));
  close(err_pipe[0]);
  if (n > 0) {  // the child failed before exec
    (void)waitpid(pid, nullptr, 0);  // reap the failed child
    const char* what = "exec";
    switch (failure.stage) {
      case FailStage::kChdir: what = "chdir"; break;
      case FailStage::kOpenStdout: what = "open stdout"; break;
      case FailStage::kOpenStderr: what = "open stderr"; break;
      case FailStage::kDupStdin: what = "dup2 stdin"; break;
      case FailStage::kDupStdout: what = "dup2 stdout"; break;
      case FailStage::kDupStderr: what = "dup2 stderr"; break;
      case FailStage::kExec: what = "exec"; break;
    }
    result.error = std::string("child failed to start (") + what + "): " +
                   std::strerror(failure.errno_value);
    if (result.stdin_fd != -1) {
      close(result.stdin_fd);
      result.stdin_fd = -1;
    }
    result.pid = -1;
    return result;
  }
  if (n < 0) {
    result.error = std::string("read(err_pipe): ") + std::strerror(errno);
  }
  return result;
}

// Polls waitpid(WNOHANG) until `deadline`; returns the moment the child is
// reaped, or reaped=false if the deadline passed first. The child is left
// untouched otherwise (no signals sent). Waits use poll(2), not sleeps.
inline WaitResult WaitPid(pid_t pid, std::chrono::steady_clock::time_point deadline) {
  WaitResult result;
  for (;;) {
    int status = 0;
    const pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      result.reaped = true;
      if (WIFEXITED(status)) {
        result.exited = true;
        result.exit_code = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        result.signaled = true;
        result.term_signal = WTERMSIG(status);
      }
      return result;
    }
    if (r < 0 && errno == ECHILD) {
      result.error = "waitpid: ECHILD (not our child?)";
      return result;
    }
    if (std::chrono::steady_clock::now() >= deadline) return result;
    poll(nullptr, 0, 10);  // 10 ms event-driven wait; EINTR-safe by contract
  }
}

// SIGTERM, escalating to SIGKILL, with `grace` per stage; the child is
// always reaped before this returns unless waitpid itself errors. Marked
// reaped=false only on a pathological failure.
inline WaitResult Terminate(pid_t pid,
                            std::chrono::milliseconds grace = std::chrono::milliseconds(2000)) {
  WaitResult result = WaitPid(pid, std::chrono::steady_clock::now() + grace);
  if (result.reaped) return result;

  result.escalated = true;
  if (kill(pid, SIGTERM) != 0) {
    result.error = std::string("kill(SIGTERM): ") + std::strerror(errno);
  }
  result = WaitPid(pid, std::chrono::steady_clock::now() + grace);
  if (result.reaped) return result;

  // SIGTERM failed to end it (or was blocked): SIGKILL cannot be blocked.
  if (kill(pid, SIGKILL) != 0) {
    result.error = std::string("kill(SIGKILL): ") + std::strerror(errno);
  }
  result = WaitPid(pid, std::chrono::steady_clock::now() + grace);
  return result;
}

// Immediate SIGKILL + reap (for paths where a graceful shutdown is
// explicitly not wanted, e.g. teardown after a failed assertion).
inline WaitResult Kill(pid_t pid,
                       std::chrono::milliseconds grace = std::chrono::milliseconds(2000)) {
  WaitResult result;
  if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
    result.error = std::string("kill(SIGKILL): ") + std::strerror(errno);
  }
  result = WaitPid(pid, std::chrono::steady_clock::now() + grace);
  return result;
}

}  // namespace process
}  // namespace sms::test
