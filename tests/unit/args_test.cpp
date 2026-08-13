#include "common/args.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace sms::common {
namespace {

std::vector<const char*> ToArgv(const std::vector<std::string>& args,
                                std::vector<char*>* storage) {
  std::vector<const char*> argv;
  argv.reserve(args.size());
  for (const std::string& s : args) {
    storage->push_back(const_cast<char*>(s.c_str()));
    argv.push_back(storage->back());
  }
  return argv;
}

Args Parse(const std::vector<std::string>& args) {
  std::vector<char*> storage;
  const std::vector<const char*> argv = ToArgv(args, &storage);
  return ParseArgs(static_cast<int>(argv.size()), const_cast<char**>(argv.data()),
                   {"host", "port", "log-level", "log-file"});
}

TEST(ArgsTest, ParsesValueFlag) {
  const Args args = Parse({"prog", "--port", "9000"});
  EXPECT_FALSE(args.error);
  EXPECT_FALSE(args.show_help);
  EXPECT_FALSE(args.version);
  EXPECT_EQ("9000", args.values.at("port"));
}

TEST(ArgsTest, UnknownFlag_SetsError) {
  const Args args = Parse({"prog", "--bogus"});
  EXPECT_TRUE(args.error);
  EXPECT_FALSE(args.error_message.empty());
  EXPECT_TRUE(args.values.empty());
}

TEST(ArgsTest, Help_SetsShowHelp) {
  const Args args = Parse({"prog", "--help"});
  EXPECT_TRUE(args.show_help);
  EXPECT_FALSE(args.error);
  EXPECT_FALSE(args.version);
}

TEST(ArgsTest, RepeatedFlag_LastWins) {
  const Args args = Parse({"prog", "--port", "9000", "--port", "9001"});
  EXPECT_FALSE(args.error);
  EXPECT_EQ("9001", args.values.at("port"));
}

TEST(ArgsTest, MissingValue_SetsError) {
  const Args args = Parse({"prog", "--host"});
  EXPECT_TRUE(args.error);
  EXPECT_FALSE(args.error_message.empty());
}

}  // namespace
}  // namespace sms::common
