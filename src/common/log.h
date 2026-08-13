#pragma once

#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

namespace sms::common {

enum class LogLevel { kTrace, kDebug, kInfo, kWarn, kError, kOff };

// Parses "trace|debug|info|warn|error|off"; returns false on unknown input.
bool ParseLogLevel(std::string_view text, LogLevel* out);

// Idempotent. Server: stdout color + optional rotating file sink.
void InitServerLogging(LogLevel level, const std::string& log_file = {});
// Idempotent. Client: file sink only (never console). Default: sms-client.log.
void InitClientLogging(LogLevel level, const std::string& log_file = {});

}  // namespace sms::common

#define SMS_LOG_TRACE(...) \
  SPDLOG_LOGGER_CALL(spdlog::get("sms"), spdlog::level::trace, __VA_ARGS__)
#define SMS_LOG_DEBUG(...) \
  SPDLOG_LOGGER_CALL(spdlog::get("sms"), spdlog::level::debug, __VA_ARGS__)
#define SMS_LOG_INFO(...) \
  SPDLOG_LOGGER_CALL(spdlog::get("sms"), spdlog::level::info, __VA_ARGS__)
#define SMS_LOG_WARN(...) \
  SPDLOG_LOGGER_CALL(spdlog::get("sms"), spdlog::level::warn, __VA_ARGS__)
#define SMS_LOG_ERROR(...) \
  SPDLOG_LOGGER_CALL(spdlog::get("sms"), spdlog::level::err, __VA_ARGS__)