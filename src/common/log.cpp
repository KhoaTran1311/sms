#include "common/log.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr const char* kLoggerName = "sms";
constexpr std::size_t kMaxFileBytes = 5 * 1024 * 1024;
constexpr std::size_t kMaxFiles = 3;

spdlog::level::level_enum ToSpdlogLevel(sms::common::LogLevel level) {
  switch (level) {
    case sms::common::LogLevel::kTrace: return spdlog::level::trace;
    case sms::common::LogLevel::kDebug: return spdlog::level::debug;
    case sms::common::LogLevel::kInfo: return spdlog::level::info;
    case sms::common::LogLevel::kWarn: return spdlog::level::warn;
    case sms::common::LogLevel::kError: return spdlog::level::err;
    case sms::common::LogLevel::kOff: return spdlog::level::off;
  }
  return spdlog::level::off;
}
}  // namespace

namespace sms::common {

bool ParseLogLevel(std::string_view text, LogLevel* out) {
  if (text == "trace") *out = LogLevel::kTrace;
  else if (text == "debug") *out = LogLevel::kDebug;
  else if (text == "info") *out = LogLevel::kInfo;
  else if (text == "warn") *out = LogLevel::kWarn;
  else if (text == "error") *out = LogLevel::kError;
  else if (text == "off") *out = LogLevel::kOff;
  else return false;
  return true;
}

void InitServerLogging(LogLevel level, const std::string& log_file) {
  auto logger = spdlog::get(kLoggerName);
  if (logger) return;  // idempotent
  std::vector<spdlog::sink_ptr> sinks{std::make_shared<spdlog::sinks::stdout_color_sink_mt>()};
  if (!log_file.empty()) {
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_file, kMaxFileBytes, kMaxFiles));
  }
  logger = std::make_shared<spdlog::logger>(kLoggerName, sinks.begin(), sinks.end());
  logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%n] %v");
  logger->set_level(ToSpdlogLevel(level));
  spdlog::register_logger(logger);
  spdlog::set_default_logger(logger);
}

void InitClientLogging(LogLevel level, const std::string& log_file) {
  auto logger = spdlog::get(kLoggerName);
  if (logger) return;  // idempotent
  const std::string file = log_file.empty() ? "sms-client.log" : log_file;
  std::vector<spdlog::sink_ptr> sinks{
      std::make_shared<spdlog::sinks::rotating_file_sink_mt>(file, kMaxFileBytes, kMaxFiles)};
  logger = std::make_shared<spdlog::logger>(kLoggerName, sinks.begin(), sinks.end());
  logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%n] %v");
  logger->set_level(ToSpdlogLevel(level));
  spdlog::register_logger(logger);
  spdlog::set_default_logger(logger);
}

}  // namespace sms::common