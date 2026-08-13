#include <common/args.h>
#include <common/log.h>
#include <server/server.h>

#include <asio.hpp>

#include <cstdint>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace {

constexpr const char* kUsage =
    "Usage: serverd [options]\n"
    "  --host <addr>     listen address (default 127.0.0.1)\n"
    "  --port <port>     listen port (default 9000; 0 = ephemeral)\n"
    "  --log-level <lvl> trace|debug|info|warn|error|off (default info)\n"
    "  --log-file <f>    log file (default: none, stdout only)\n"
    "  --help            show this help and exit\n"
    "  --version         show version and exit\n";

std::string Flag(const std::map<std::string, std::string>& values,
                 const char* name, const char* fallback) {
  const auto it = values.find(name);
  return it == values.end() ? fallback : it->second;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    auto args = sms::common::ParseArgs(
        argc, argv, {"host", "port", "log-level", "log-file"});
    if (args.show_help || args.version) {
      if (args.version) {
        std::cout << "serverd " << SMS_VERSION << "\n";
      }
      std::cout << kUsage;
      return 0;
    }
    if (args.error) {
      std::cerr << "serverd: " << args.error_message << "\n"
                << "try 'serverd --help'\n";
      return 2;
    }

    sms::common::LogLevel level = sms::common::LogLevel::kInfo;
    if (!sms::common::ParseLogLevel(Flag(args.values, "log-level", "info"),
                                    &level)) {
      std::cerr << "serverd: bad --log-level: "
                << Flag(args.values, "log-level", "info") << "\n";
      return 2;
    }
    sms::common::InitServerLogging(
        level, Flag(args.values, "log-file", ""));

    asio::io_context io;
    const std::string host = Flag(args.values, "host", "127.0.0.1");
    const int raw_port = std::stoi(Flag(args.values, "port", "9000"));
    if (raw_port < 0 || raw_port > 65535) {
      std::cerr << "serverd: invalid port: " << raw_port << "\n";
      return 2;
    }
    const auto port = static_cast<std::uint16_t>(raw_port);

    auto server = std::make_shared<sms::server::Server>(io, host, port);

    auto result = server->Start();
    if (!result.ok()) {
      SMS_LOG_ERROR("startup failed: {}", result.error().message);
      return 1;
    }
    SMS_LOG_INFO("serverd listening on {}:{}", host, port);

    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](const asio::error_code& ec, int) {
      if (ec) return;
      SMS_LOG_INFO("signal received, shutting down");
      server->Stop();
      io.stop();  // run() returns; main exits 0
    });

    io.run();
    SMS_LOG_INFO("serverd exited cleanly");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "serverd: fatal: " << e.what() << "\n";  // no logger yet if init failed
    return 1;
  }
}
