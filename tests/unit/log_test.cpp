#include "common/log.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

#include <gtest/gtest.h>

using namespace sms::common;

namespace {
std::string LogPath(const std::string& name) {
  return (std::filesystem::path(testing::TempDir()) /
      ("sms_" + name + "_" + std::to_string(::getpid()) + ".log"))
    .string();
}

std::string ReadFile(const std::string& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int CountFilesWithPrefix(const std::string& path) {
  const std::string base = path.substr(0, path.size() - 4);
  int count = 0;
  const std::filesystem::path dir = std::filesystem::path(path).parent_path();
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.path().string().rfind(base, 0) == 0) ++count;
  }
  return count;
}

class LogTest : public ::testing::Test {
protected:
  void TearDown() override { spdlog::shutdown(); }
};
} // namespace

TEST_F(LogTest, ParseLogLevel_Valid_ReturnsTrue) {
  LogLevel out = LogLevel::Off;
  EXPECT_TRUE(ParseLogLevel("trace", &out));
  EXPECT_EQ(out, LogLevel::Trace);
  EXPECT_TRUE(ParseLogLevel("debug", &out));
  EXPECT_EQ(out, LogLevel::Debug);
  EXPECT_TRUE(ParseLogLevel("info", &out));
  EXPECT_EQ(out, LogLevel::Info);
  EXPECT_TRUE(ParseLogLevel("warn", &out));
  EXPECT_EQ(out, LogLevel::Warn);
  EXPECT_TRUE(ParseLogLevel("error", &out));
  EXPECT_EQ(out, LogLevel::Error);
  EXPECT_TRUE(ParseLogLevel("off", &out));
  EXPECT_EQ(out, LogLevel::Off);
}

TEST_F(LogTest, ParseLogLevel_Invalid_ReturnsFalse) {
  LogLevel out = LogLevel::Info;
  EXPECT_FALSE(ParseLogLevel("verbose", &out));
  EXPECT_FALSE(ParseLogLevel("", &out));
  EXPECT_FALSE(ParseLogLevel("INFO", &out));
}

TEST_F(LogTest, InitClientLogging_WritesFile_Only) {
  const std::string path = LogPath("client_only");
  testing::internal::CaptureStdout();
  InitClientLogging(LogLevel::Info, path);
  SMS_LOG_INFO("client_only_marker");
  const std::string captured = testing::internal::GetCapturedStdout();
  spdlog::shutdown();
  EXPECT_EQ(captured, "");
  EXPECT_TRUE(std::filesystem::exists(path));
  const std::string content = ReadFile(path);
  EXPECT_NE(content.find("client_only_marker"), std::string::npos);
}

TEST_F(LogTest, InitServerLogging_RespectsLevel) {
  const std::string path = LogPath("server_level");
  InitServerLogging(LogLevel::Warn, path);
  SMS_LOG_INFO("info_marker_absent");
  SMS_LOG_WARN("warn_marker_present");
  SMS_LOG_ERROR("error_marker_present");
  spdlog::shutdown();
  const std::string content = ReadFile(path);
  EXPECT_NE(content.find("warn_marker_present"), std::string::npos);
  EXPECT_NE(content.find("error_marker_present"), std::string::npos);
  EXPECT_EQ(content.find("info_marker_absent"), std::string::npos);
}

TEST_F(LogTest, InitLogging_IsIdempotent) {
  const std::string first = LogPath("idem_first");
  const std::string second = LogPath("idem_second");
  InitServerLogging(LogLevel::Info, first);
  InitClientLogging(LogLevel::Info, second);
  SMS_LOG_INFO("idempotency_marker");
  spdlog::shutdown();
  EXPECT_TRUE(std::filesystem::exists(first));
  EXPECT_FALSE(std::filesystem::exists(second));
  const std::string content = ReadFile(first);
  EXPECT_NE(content.find("idempotency_marker"), std::string::npos);
}

TEST_F(LogTest, RotatingSink_LargeLog_Rotates) {
  const std::string path = LogPath("rotating");
  InitClientLogging(LogLevel::Info, path);
  const std::string chunk(64 * 1024, 'x');
  for (int i = 0; i < 84; ++i) {
    SMS_LOG_INFO("{}", chunk);
  }
  spdlog::shutdown();
  EXPECT_EQ(CountFilesWithPrefix(path), 2);
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_TRUE(std::filesystem::exists(path.substr(0, path.size() - 4) + ".1.log"));
}
