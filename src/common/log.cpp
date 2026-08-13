#include "common/log.h"
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr auto LoggerName = "sms";
constexpr std::size_t MaxFileBytes = 5 * 1024 * 1024;
constexpr std::size_t MaxFiles = 3;

spdlog::level::level_enum ToSpdlogLevel(sms::common::LogLevel level) {
  switch (level) {
  case sms::common::LogLevel::Trace: return spdlog::level::trace;
  case sms::common::LogLevel::Debug: return spdlog::level::debug;
  case sms::common::LogLevel::Info: return spdlog::level::info;
  case sms::common::LogLevel::Warn: return spdlog::level::warn;
  case sms::common::LogLevel::Error: return spdlog::level::err;
  case sms::common::LogLevel::Off: return spdlog::level::off;
  }
  return spdlog::level::off;
}
} // namespace

namespace sms::common {
bool ParseLogLevel(std::string_view text, LogLevel* out) {
  if (text == "trace") *out = LogLevel::Trace;
  else if (text == "debug") *out = LogLevel::Debug;
  else if (text == "info") *out = LogLevel::Info;
  else if (text == "warn") *out = LogLevel::Warn;
  else if (text == "error") *out = LogLevel::Error;
  else if (text == "off") *out = LogLevel::Off;
  else return false;
  return true;
}

void InitServerLogging(LogLevel level, const std::string& log_file) {
  auto logger = spdlog::get(LoggerName);
  if (logger) return; // idempotent
  std::vector<spdlog::sink_ptr> sinks{ std::make_shared<spdlog::sinks::stdout_color_sink_mt>() };
  if (!log_file.empty()) {
    sinks.push_back(
      std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_file, MaxFileBytes, MaxFiles
      )
    );
  }
  logger = std::make_shared<spdlog::logger>(LoggerName, sinks.begin(), sinks.end());
  logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%n] %v");
  logger->set_level(ToSpdlogLevel(level));
  spdlog::register_logger(logger);
  spdlog::set_default_logger(logger);
}

void InitClientLogging(LogLevel level, const std::string& log_file) {
  auto logger = spdlog::get(LoggerName);
  if (logger) return; // idempotent
  const std::string file = log_file.empty() ? "sms-client.log" : log_file;
  std::vector<spdlog::sink_ptr> sinks{
    std::make_shared<spdlog::sinks::rotating_file_sink_mt>(file, MaxFileBytes, MaxFiles)
  };
  logger = std::make_shared<spdlog::logger>(LoggerName, sinks.begin(), sinks.end());
  logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%n] %v");
  logger->set_level(ToSpdlogLevel(level));
  spdlog::register_logger(logger);
  spdlog::set_default_logger(logger);
}
} // namespace sms::common
