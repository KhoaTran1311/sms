#include "client/client.h"
#include "common/error.h"
#include "common/result.h"
#include "support/loopback.h"
#include "support/process.h"

#include <asio.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <csignal>
#include <unistd.h>

// P0-14: end-to-end integration tests against the REAL binaries. The
// ServerdFixture spawns the built `serverd` daemon on a probed free port,
// polls readiness by connect()ing (deadline-bounded, no sleeps), and runs
// the test's own asio::io_context for the in-process sms::client::Client
// instances. Every subprocess is reaped: SIGTERM first, SIGKILL fallback,
// and the fixture destructor guarantees no zombie survives a test.
//
// The in-process Client's read/write loops are async on the caller-owned
// io_context; all waits are deadline timers (RunUntil / sms::test::WaitFor)
// — never std::this_thread::sleep_for.
//
// Binary lookup: $SMS_SERVERD_BIN / $SMS_CLI_BIN (set by ctest), falling
// back to well-known paths relative to the build tree.
namespace sms::client {
namespace {

using sms::test::process::Spawn;
using sms::test::process::WaitPid;

constexpr auto kReadyTimeout = std::chrono::milliseconds(10000);  // spawn readiness
constexpr auto kCaseTimeout = std::chrono::milliseconds(5000);    // per-wait deadline
constexpr auto kQuiet = std::chrono::milliseconds(250);           // quiet-period window

// Timer-driven poll of connect() against an endpoint: retries every 50 ms
// on connection_refused until one succeeds or the deadline passes. The
// poller keeps itself alive through its async chains and cleans up all
// pending work before the completion handler runs (see Finish()).
class ConnectPoller : public std::enable_shared_from_this<ConnectPoller> {
 public:
  using Deadline = std::chrono::steady_clock::time_point;

  static std::shared_ptr<ConnectPoller> Start(
      asio::io_context& io, const asio::ip::tcp::endpoint& endpoint,
      Deadline deadline, std::function<void(bool)> handler) {
    auto poller = std::shared_ptr<ConnectPoller>(
        new ConnectPoller(io, endpoint, deadline, std::move(handler)));
    asio::post(io, [poller] { poller->TryConnect(); });
    return poller;
  }

 private:
  ConnectPoller(asio::io_context& io, const asio::ip::tcp::endpoint& endpoint,
                Deadline deadline, std::function<void(bool)> handler)
      : io_(io), endpoint_(endpoint), deadline_(deadline),
        handler_(std::move(handler)), socket_(io), timer_(io) {}

  // Drops the user callback and marks the poller done so a handler chain
  // left behind by an early test exit becomes a no-op. Only runs once every
  // chain that holds a shared_ptr has released it.
  ~ConnectPoller() { done_ = true; handler_ = nullptr; }

  static bool Retryable(const asio::error_code& ec) {
    return ec == asio::error::connection_refused ||
           ec == asio::error::connection_reset ||
           ec == asio::error::timed_out ||
           ec == asio::error::network_unreachable ||
           ec == asio::error::host_unreachable;
  }

  void TryConnect() {
    if (done_) return;
    if (std::chrono::steady_clock::now() >= deadline_) return Finish();
    socket_ = asio::ip::tcp::socket(io_);
    socket_.async_connect(endpoint_, [self = shared_from_this()](
                                         const asio::error_code& ec) {
      if (self->done_) return;
      asio::error_code ignored;
      self->socket_.close(ignored);
      if (!ec) return self->Finish();
      if (Retryable(ec) &&
          std::chrono::steady_clock::now() < self->deadline_) {
        self->timer_.expires_after(std::chrono::milliseconds(50));
        self->timer_.async_wait([self](const asio::error_code& tec) {
          if (tec || self->done_) return;
          self->TryConnect();
        });
        return;
      }
      self->Finish();
    });
  }

  // Exactly-once completion: cancel the retry timer, close the scratch
  // socket, and fire the user handler.
  void Finish() {
    if (done_) return;
    done_ = true;
    asio::error_code ignored;
    timer_.cancel(ignored);
    socket_.close(ignored);
    auto handler = std::move(handler_);
    handler_ = nullptr;
    handler(true);
  }

  asio::io_context& io_;
  asio::ip::tcp::endpoint endpoint_;
  Deadline deadline_;
  std::function<void(bool)> handler_;
  asio::ip::tcp::socket socket_;
  asio::steady_timer timer_;
  bool done_ = false;
};

// Binds 127.0.0.1:0, reads the assigned port, and closes — the free-port
// probe. The probe-close-spawn gap is microseconds; on connect-refused the
// fixture retries the whole probe once (see StartDaemon).
uint16_t ProbeFreePort() {
  asio::io_context io;
  asio::ip::tcp::acceptor acceptor(
      io, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

// Resolves a binary path: $ENV first, then well-known build-tree paths
// (ctest runs tests from build/dev/tests; direct runs from build/dev).
std::filesystem::path ResolveBinary(
    const char* env, std::initializer_list<const char*> relative) {
  if (const char* e = std::getenv(env); e != nullptr && *e != '\0') {
    return std::filesystem::path(e);
  }
  for (const char* rel : relative) {
    std::filesystem::path p = std::filesystem::current_path() / rel;
    if (std::filesystem::is_regular_file(p)) return p;
  }
  return {};
}

class ServerdFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    // A test harness that pipes into children must never die to SIGPIPE
    // (e.g. the CLI exiting before our stdin write lands); report EPIPE as
    // an assertion failure instead.
    ::signal(SIGPIPE, SIG_IGN);

    tmpdir_ = std::filesystem::temp_directory_path() /
              ("sms-p0-14-" + std::to_string(::getpid()) + "-" +
               std::to_string(instance_++));
    std::filesystem::create_directories(tmpdir_);

    serverd_ = ResolveBinary("SMS_SERVERD_BIN",
                             {"../apps/serverd/serverd", "apps/serverd/serverd"});
    cli_ = ResolveBinary("SMS_CLI_BIN",
                         {"../apps/client-cli/sms-cli", "apps/client-cli/sms-cli"});
    ASSERT_FALSE(serverd_.empty())
        << "serverd binary not found; ctest sets SMS_SERVERD_BIN";
    ASSERT_FALSE(cli_.empty())
        << "sms-cli binary not found; ctest sets SMS_CLI_BIN";
  }

  void TearDown() override {
    StopDaemon();  // SIGTERM -> SIGKILL -> reap; never leaves a zombie
    std::error_code ec;
    std::filesystem::remove_all(tmpdir_, ec);
  }

  asio::ip::tcp::endpoint Endpoint() const {
    return asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"),
                                   daemon_port_);
  }

  // Probes a free port, spawns the daemon on it, and waits until it
  // accepts connections (10 s deadline). On failure — including the daemon
  // dying because the port was stolen in the probe-close-spawn gap — reaps
  // it and retries the probe once.
  bool StartDaemon() {
    uint16_t port = ProbeFreePort();
    for (int attempt = 0; attempt < 2; ++attempt) {
      if (SpawnDaemon(port) && WaitReady()) return true;
      DumpDaemonDiagnostics(attempt);
      StopDaemon();
      port = ProbeFreePort();
    }
    return false;
  }

  // Spawns the daemon on an explicit port (used by the restart case, which
  // must reuse the same port).
  bool SpawnDaemon(uint16_t port) {
    auto opts = SpawnOpts("serverd");
    daemon_ = Spawn(serverd_,
                    {"--host", "127.0.0.1", "--port", std::to_string(port),
                     "--log-level", "debug",
                     "--log-file", (tmpdir_ / "serverd.log").string()},
                    opts);
    if (!daemon_.error.empty()) {
      ADD_FAILURE() << "spawn serverd: " << daemon_.error;
      return false;
    }
    daemon_port_ = port;
    daemon_running_ = true;
    return true;
  }

  // Polls connect() until the daemon accepts (deadline-bounded, no sleeps).
  bool WaitReady() {
    if (!daemon_running_) return false;
    bool ok = false;
    auto poller = ConnectPoller::Start(
        io_, Endpoint(), std::chrono::steady_clock::now() + kReadyTimeout,
        [&ok](bool ready) { ok = ready; });
    io_.run_for(kReadyTimeout + std::chrono::seconds(1));
    io_.restart();
    return ok;
  }

  // SIGTERM (escalating to SIGKILL) the daemon and reap it. Every child is
  // reaped here — a zombie after TearDown is a fixture bug.
  bool StopDaemon() {
    if (!daemon_running_) return true;
    daemon_running_ = false;
    auto wr = sms::test::process::Terminate(daemon_.pid);
    if (!wr.reaped || !wr.error.empty()) {
      ADD_FAILURE() << "daemon pid " << daemon_.pid
                    << " not reaped: " << wr.error;
      return false;
    }
    return true;
  }

  struct ClientHandle {
    std::shared_ptr<Client> client;
    std::vector<std::string> messages;
    int close_count = 0;
    bool connect_ok = false;
    bool connect_done = false;
    std::string connect_error;
  };

  // In-process client connected to the daemon; async, driven by RunUntil.
  std::shared_ptr<ClientHandle> ConnectClient() {
    auto h = std::make_shared<ClientHandle>();
    h->client = std::make_shared<Client>(
        io_,
        [h](std::string payload) { h->messages.push_back(std::move(payload)); },
        [h]() { ++h->close_count; });
    h->client->Connect(Endpoint(), [h](sms::common::Error err) {
      h->connect_ok = err.code == sms::common::ErrorCode::kNone;
      h->connect_error = err.message;
      h->connect_done = true;
    });
    return h;
  }

  // Pumps io_ until `pred` is true or `timeout` elapses; deadline-timer
  // driven (sms::test::WaitFor), never sleeps. Returns whether pred won.
  bool RunUntil(std::function<bool()> pred, std::chrono::milliseconds timeout) {
    bool done = false;
    sms::test::WaitFor(
        io_, std::move(pred), std::chrono::steady_clock::now() + timeout,
        [&done](bool ok) { done = ok; });
    io_.run_for(timeout + std::chrono::seconds(1));
    io_.restart();
    return done;
  }

  process::SpawnOptions SpawnOpts(const char* tag, bool pipe_stdin = false) {
    process::SpawnOptions opts;
    opts.cwd = tmpdir_;
    opts.stdout_file = tmpdir_ / (std::string(tag) + ".stdout.log");
    opts.stderr_file = tmpdir_ / (std::string(tag) + ".stderr.log");
    opts.pipe_stdin = pipe_stdin;
    return opts;
  }

  std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
  }

  // Non-fatal diagnostics dumped when a daemon never becomes ready: its
  // exit status (if it already died) and the tail of its captured logs.
  void DumpDaemonDiagnostics(int attempt) {
    auto wr = WaitPid(daemon_.pid, std::chrono::steady_clock::now() + kCaseTimeout);
    if (wr.reaped) {
      ADD_FAILURE() << "serverd attempt " << attempt << " exited early: "
                    << (wr.exited ? "code " + std::to_string(wr.exit_code)
                                  : "signal " + std::to_string(wr.term_signal));
    } else {
      ADD_FAILURE() << "serverd attempt " << attempt
                    << " never became ready (still running)";
    }
    const std::string out = ReadFile(tmpdir_ / "serverd.stdout.log");
    const std::string err = ReadFile(tmpdir_ / "serverd.stderr.log");
    if (!out.empty()) {
      ADD_FAILURE() << "serverd stdout:\n"
                    << out.substr(out.size() > 2048 ? out.size() - 2048 : 0);
    }
    if (!err.empty()) {
      ADD_FAILURE() << "serverd stderr:\n" << err;
    }
  }

  asio::io_context io_;
  std::filesystem::path tmpdir_;
  std::filesystem::path serverd_;
  std::filesystem::path cli_;
  process::SpawnResult daemon_;
  bool daemon_running_ = false;
  uint16_t daemon_port_ = 0;
  inline static int instance_ = 0;
};

TEST_F(ServerdFixture, TwoClients_Relay_RoundTrip) {
  ASSERT_TRUE(StartDaemon());

  auto a = ConnectClient();
  auto b = ConnectClient();
  ASSERT_TRUE(RunUntil([&] { return a->connect_done && b->connect_done; },
                       kCaseTimeout));
  ASSERT_TRUE(a->connect_ok) << a->connect_error;
  ASSERT_TRUE(b->connect_ok) << b->connect_error;

  ASSERT_TRUE(a->client->Send("ping").ok());
  ASSERT_TRUE(RunUntil([&] { return !b->messages.empty(); }, kCaseTimeout))
      << "B never received the relayed frame";
  EXPECT_EQ(1u, b->messages.size());
  EXPECT_EQ("ping", b->messages[0]);
  EXPECT_EQ(0u, a->messages.size());  // origin exclusion

  // Quiet period: any (incorrect) delivery to A would land here.
  RunUntil([&] { return false; }, kQuiet);
  EXPECT_EQ(0u, a->messages.size());
  EXPECT_EQ(0, a->close_count);
  EXPECT_EQ(0, b->close_count);
}

TEST_F(ServerdFixture, ClientReconnect_AfterServerRestart_NotInPoC) {
  ASSERT_TRUE(StartDaemon());

  auto a = ConnectClient();
  ASSERT_TRUE(RunUntil([&] { return a->connect_done; }, kCaseTimeout));
  ASSERT_TRUE(a->connect_ok) << a->connect_error;

  // Kill the daemon: the client must observe the disconnect exactly once.
  ASSERT_TRUE(StopDaemon());
  ASSERT_TRUE(RunUntil([&] { return a->close_count >= 1; }, kCaseTimeout))
      << "on_close never fired after the daemon died";

  EXPECT_EQ(1, a->close_count);  // P0-12 exactly-once guarantee
  EXPECT_FALSE(a->client->connected());

  // Reconnect is Phase 1 — assert the *hook* works, i.e. Send now reports
  // the disconnection instead of buffering silently.
  auto res = a->client->Send("too late");
  ASSERT_FALSE(res.ok());
  EXPECT_EQ(sms::common::ErrorCode::kDisconnected, res.error().code);
}

TEST_F(ServerdFixture, LargeMessage_64KiB) {
  ASSERT_TRUE(StartDaemon());

  auto a = ConnectClient();
  auto b = ConnectClient();
  ASSERT_TRUE(RunUntil([&] { return a->connect_done && b->connect_done; },
                       kCaseTimeout));
  ASSERT_TRUE(a->connect_ok) << a->connect_error;
  ASSERT_TRUE(b->connect_ok) << b->connect_error;

  // 60 KiB (61440 bytes) stays under the 64 KiB codec cap (kMaxPayloadSize).
  const std::string payload(60 * 1024, 'x');
  ASSERT_TRUE(a->client->Send(payload).ok());
  ASSERT_TRUE(RunUntil([&] { return !b->messages.empty(); }, kCaseTimeout))
      << "B never received the 60 KiB payload";
  ASSERT_EQ(1u, b->messages.size());
  EXPECT_EQ(payload, b->messages[0]);  // byte-identical end to end
}

TEST_F(ServerdFixture, Serverd_Help_ExitsZero) {
  auto sp = Spawn(serverd_, {"--help"}, SpawnOpts("help"));
  ASSERT_TRUE(sp.error.empty()) << sp.error;

  auto wr = WaitPid(sp.pid, std::chrono::steady_clock::now() + kCaseTimeout);
  ASSERT_TRUE(wr.reaped) << "serverd --help did not exit in time";
  ASSERT_TRUE(wr.exited) << "serverd --help terminated by signal "
                         << wr.term_signal;
  EXPECT_EQ(0, wr.exit_code);
  const std::string out = ReadFile(tmpdir_ / "help.stdout.log");
  EXPECT_NE(std::string::npos, out.find("Usage: serverd")) << out;
}

TEST_F(ServerdFixture, Serverd_ExitCode1_OnBindFailure) {
  // Occupy a port for real, then ask the daemon to bind it: bind fails,
  // startup aborts with exit code 1.
  asio::io_context blocker_io;
  asio::ip::tcp::acceptor blocker(
      blocker_io, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"),
                                          0));
  const uint16_t port = blocker.local_endpoint().port();

  auto sp = Spawn(serverd_,
                  {"--host", "127.0.0.1", "--port", std::to_string(port)},
                  SpawnOpts("bindfail"));
  ASSERT_TRUE(sp.error.empty()) << sp.error;

  auto wr = WaitPid(sp.pid, std::chrono::steady_clock::now() + kCaseTimeout);
  ASSERT_TRUE(wr.reaped) << "serverd did not exit on bind failure";
  ASSERT_TRUE(wr.exited) << "serverd terminated by signal " << wr.term_signal;
  EXPECT_EQ(1, wr.exit_code);

  const std::string out = ReadFile(tmpdir_ / "bindfail.stdout.log");
  const std::string err = ReadFile(tmpdir_ / "bindfail.stderr.log");
  EXPECT_NE(std::string::npos, (out + err).find("startup failed"))
      << "stdout:\n" << out << "\nstderr:\n" << err;
}

TEST_F(ServerdFixture, Serverd_SIGTERM_ExitsZero) {
  ASSERT_TRUE(StartDaemon());

  ASSERT_EQ(0, ::kill(daemon_.pid, SIGTERM));
  auto wr = WaitPid(daemon_.pid, std::chrono::steady_clock::now() + kCaseTimeout);
  ASSERT_TRUE(wr.reaped) << "serverd did not exit after SIGTERM";
  ASSERT_TRUE(wr.exited) << "serverd terminated by signal " << wr.term_signal;
  EXPECT_EQ(0, wr.exit_code);  // graceful shutdown path (D0.10)

  daemon_running_ = false;  // already reaped; TearDown must not re-signal
}

TEST_F(ServerdFixture, Cli_WithPipedStdin_ConnectsAndSends) {
  ASSERT_TRUE(StartDaemon());

  // An in-process observer proves the CLI's frame reached the daemon.
  auto observer = ConnectClient();
  ASSERT_TRUE(RunUntil([&] { return observer->connect_done; }, kCaseTimeout));
  ASSERT_TRUE(observer->connect_ok) << observer->connect_error;

  auto opts = SpawnOpts("cli", /*pipe_stdin=*/true);
  auto cli = Spawn(cli_,
                   {"--host", "127.0.0.1", "--port",
                    std::to_string(daemon_port_), "--log-level", "off",
                    "--log-file", (tmpdir_ / "cli.log").string()},
                   opts);
  ASSERT_TRUE(cli.error.empty()) << cli.error;

  // One line, then EOF: the REPL sends the line and exits via the EOF path.
  const std::string input = "hello\n";
  const ssize_t n = ::write(cli.stdin_fd, input.data(), input.size());
  ASSERT_EQ(static_cast<ssize_t>(input.size()), n)
      << (n < 0 ? std::strerror(errno) : "short write");
  ASSERT_EQ(0, ::close(cli.stdin_fd));

  // The frame must arrive at the observer (the CLI connected and sent).
  ASSERT_TRUE(RunUntil([&] { return !observer->messages.empty(); },
                       kCaseTimeout))
      << "no frame observed from the piped-stdin CLI";
  EXPECT_EQ(1u, observer->messages.size());
  EXPECT_EQ("hello", observer->messages[0]);

  auto wr = WaitPid(cli.pid, std::chrono::steady_clock::now() + kCaseTimeout);
  ASSERT_TRUE(wr.reaped)
      << "sms-cli did not exit; stdout:\n"
      << ReadFile(tmpdir_ / "cli.stdout.log") << "\nstderr:\n"
      << ReadFile(tmpdir_ / "cli.stderr.log");
  ASSERT_TRUE(wr.exited);
  EXPECT_EQ(0, wr.exit_code);
}

TEST_F(ServerdFixture, ServerRestart_Survives) {
  ASSERT_TRUE(StartDaemon());
  const uint16_t port = daemon_port_;

  // Client A works against daemon #1.
  auto a = ConnectClient();
  ASSERT_TRUE(RunUntil([&] { return a->connect_done; }, kCaseTimeout));
  ASSERT_TRUE(a->connect_ok) << a->connect_error;
  ASSERT_TRUE(a->client->Send("pre-restart").ok());

  // SIGTERM daemon #1 and spawn daemon #2 on the SAME port.
  ASSERT_TRUE(StopDaemon());
  ASSERT_TRUE(SpawnDaemon(port)) << "failed to respawn on port " << port;
  ASSERT_TRUE(WaitReady());

  // Both fresh clients work after the restart — no state carried over.
  auto b = ConnectClient();
  auto c = ConnectClient();
  ASSERT_TRUE(RunUntil([&] { return b->connect_done && c->connect_done; },
                       kCaseTimeout));
  ASSERT_TRUE(b->connect_ok) << b->connect_error;
  ASSERT_TRUE(c->connect_ok) << c->connect_error;

  ASSERT_TRUE(b->client->Send("hello-after-restart").ok());
  ASSERT_TRUE(RunUntil([&] { return !c->messages.empty(); }, kCaseTimeout))
      << "C never received the post-restart relay";
  EXPECT_EQ(1u, c->messages.size());
  EXPECT_EQ("hello-after-restart", c->messages[0]);
  EXPECT_EQ(0u, b->messages.size());  // origin exclusion holds after restart
}

}  // namespace
}  // namespace sms::client
