#include "common/error.h"
#include "server/server.h"

#include <gtest/gtest.h>

#include <asio.hpp>

#include <memory>
#include <string>

namespace sms::server {
namespace {

// Start() must never throw: bad host maps to kInvalidArgument.
TEST(ServerStartTest, Start_BadHost_ReturnsError) {
  asio::io_context io;
  Server server(io, "not-an-ip", 9000);

  const sms::common::Result<void> result = server.Start();

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(sms::common::ErrorCode::kInvalidArgument, result.error().code);
}

// Binding a port already in use maps to kBindFailed (no throw).
TEST(ServerStartTest, Start_PortInUse_ReturnsBindError) {
  asio::io_context io;
  auto first = std::make_shared<Server>(io, "127.0.0.1", 0);
  const sms::common::Result<void> first_start = first->Start();
  ASSERT_TRUE(first_start.ok());
  ASSERT_TRUE(first->LocalEndpoint().port() != 0);  // port 0 -> ephemeral

  Server second(io, "127.0.0.1", first->LocalEndpoint().port());
  const sms::common::Result<void> second_start = second.Start();

  ASSERT_FALSE(second_start.ok());
  EXPECT_EQ(sms::common::ErrorCode::kBindFailed, second_start.error().code);

  first->Stop();
}

}  // namespace
}  // namespace sms::server
