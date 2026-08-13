#include <client/client.h>
#include <common/args.h>
#include <common/log.h>

#include <asio.hpp>

#include <cstdint>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <string>

#include <unistd.h>

namespace {

constexpr const char* kUsage =
    "Usage: sms-cli [options]\n"
    "  --host <addr>     server address (default 127.0.0.1)\n"
    "  --port <port>     server port (default 9000)\n"
    "  --log-level <lvl> trace|debug|info|warn|error|off (default info)\n"
    "  --log-file <f>    log file (default sms-client.log)\n"
    "  --help            show this help and exit\n"
    "  --version         show version and exit\n"
    "\n"
    "Slash commands: /quit, /help, /whoami\n";

std::string Flag(const std::map<std::string, std::string>& values,
                 const char* name, const char* fallback) {
  const auto it = values.find(name);
  return it == values.end() ? fallback : it->second;
}

// Renders an incoming payload as "[HH:MM:SS] <payload>" with a local-time
// timestamp, then redraws the prompt so it never disappears mid-line.
void PrintIncoming(const std::string& payload) {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
  std::cout << "[" << buf << "] " << payload << "\n";
  std::cout << "> " << std::flush;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    auto args = sms::common::ParseArgs(
        argc, argv, {"host", "port", "log-level", "log-file"});
    if (args.show_help || args.version) {
      if (args.version) {
        std::cout << "sms-cli " << SMS_CLI_VERSION << "\n";
      }
      std::cout << kUsage;
      return 0;
    }
    if (args.error) {
      std::cerr << "sms-cli: " << args.error_message << "\n"
                << "try 'sms-cli --help'\n";
      return 2;
    }

    sms::common::LogLevel level = sms::common::LogLevel::kInfo;
    if (!sms::common::ParseLogLevel(Flag(args.values, "log-level", "info"),
                                    &level)) {
      level = sms::common::LogLevel::kInfo;
    }
    // File-only logging (P0-02) keeps chat output and logs from interleaving.
    sms::common::InitClientLogging(level,
                                   Flag(args.values, "log-file", "sms-client.log"));

    const std::string host = Flag(args.values, "host", "127.0.0.1");
    const int raw_port = std::stoi(Flag(args.values, "port", "9000"));
    if (raw_port < 1 || raw_port > 65535) {
      std::cerr << "sms-cli: invalid port: " << raw_port << "\n";
      return 2;
    }
    const auto port = static_cast<std::uint16_t>(raw_port);
    const auto endpoint =
        asio::ip::tcp::endpoint(asio::ip::make_address(host), port);

    asio::io_context io;

    asio::posix::stream_descriptor stdin_(io, ::dup(STDIN_FILENO));
    std::string line;

    auto client = std::make_shared<sms::client::Client>(
        io,
        /* on_message */ [](std::string payload) { PrintIncoming(payload); },
        /* on_close   */ [&] {
          // Server went away (or we quit): stop the stdin loop so io.run()
          // drains and main returns with a clean exit.
          stdin_.cancel();
        });

    auto read_line = [&](auto&& self) -> void {
      asio::async_read_until(
          stdin_, asio::dynamic_buffer(line), '\n',
          [&, self](const asio::error_code& ec, std::size_t n) {
            if (ec) {  // EOF (Ctrl-D) or cancelled by on_close
              client->Disconnect();
              return;
            }
            // The dynamic buffer holds everything read so far; only the
            // first n bytes end at the delimiter, so consume exactly those
            // and keep any later lines buffered for the next read.
            std::string input = line.substr(0, n);
            line.erase(0, n);
            if (!input.empty() && input.back() == '\n') input.pop_back();
            if (!input.empty() && input.back() == '\r') input.pop_back();

            if (input == "/quit") {
              client->Disconnect();
              return;
            }
            if (input == "/help") {
              std::cout << kUsage << "> " << std::flush;
            } else if (input == "/whoami") {
              std::cout << "connected to " << host << ":" << port
                        << "\n> " << std::flush;
            } else if (input.rfind("/", 0) == 0) {
              std::cout << "unknown command: " << input
                        << " (try /help)\n> " << std::flush;
            } else {
              auto result = client->Send(input);
              if (!result.ok()) {
                std::cout << "send failed: " << result.error().message
                          << "\n> " << std::flush;
              }
            }
            self(self);
          });
    };

    std::cout << "connecting to " << host << ":" << port << " ...\n";
    SMS_LOG_INFO("connecting to {}:{}", host, port);
    bool connect_failed = false;
    client->Connect(endpoint, [&](sms::common::Error err) {
      if (err.code != sms::common::ErrorCode::kNone) {
        std::cerr << "sms-cli: connect failed: " << err.message << "\n";
        SMS_LOG_ERROR("connect failed: {}", err.message);
        connect_failed = true;
        io.stop();
        return;
      }
      SMS_LOG_INFO("connected to {}:{}", host, port);
      std::cout << "connected. type a message; /quit to leave.\n> "
                << std::flush;
      read_line(read_line);
    });

    io.run();
    if (connect_failed) return 1;
    std::cout << "\nbye\n";
    SMS_LOG_INFO("client exiting");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "sms-cli: fatal: " << e.what() << "\n";
    return 1;
  }
}
