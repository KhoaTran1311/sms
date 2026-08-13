#include "common/error.h"
#include "net/frame_codec.h"
#include "server/session.h"
#include "support/loopback.h"

#include <gtest/gtest.h>

#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sms::server {
namespace {

// Runs `io` until its pending work completes or the watchdog fires, then
// resets it for reuse. Tests stop the io_context from a completion handler
// when their expectation is met; the watchdog guarantees a failing test
// returns instead of hanging.
void Pump(asio::io_context& io,
          std::chrono::milliseconds wait = std::chrono::milliseconds(500)) {
  asio::steady_timer watchdog(io, wait);
  watchdog.async_wait([&io](const asio::error_code&) { io.stop(); });
  io.run();
  io.restart();
}

std::string EncodeFrame(const std::string& payload) {
  sms::net::FrameCodec codec;
  std::vector<std::uint8_t> wire;
  EXPECT_TRUE(codec.Encode(payload, &wire));
  return std::string(wire.begin(), wire.end());
}

TEST(SessionTest, Send_TwoFrames_OrderPreserved) {
  asio::io_context io;
  auto [server_sock, client_sock] = sms::test::MakeLoopbackPair(io);

  const std::string frame1 = EncodeFrame("hello");
  const std::string frame2 = EncodeFrame("world!");
  const std::size_t total = frame1.size() + frame2.size();

  std::string received(total, '\0');
  bool received_all = false;
  asio::async_read(client_sock, asio::buffer(received), asio::transfer_exactly(total),
                   [&](const asio::error_code& ec, std::size_t) {
                     received_all = !ec;
                     io.stop();
                   });

  auto session = std::make_shared<Session>(
      std::move(server_sock), 1,
      [](std::shared_ptr<Session>, std::string) {},
      [](std::shared_ptr<Session>) {},
      [](std::shared_ptr<Session>, sms::common::Error) {});
  session->Start();
  session->Send(frame1);
  session->Send(frame2);

  Pump(io);

  EXPECT_TRUE(received_all);
  EXPECT_EQ(frame1 + frame2, received);
}

TEST(SessionTest, Close_Idempotent) {
  asio::io_context io;
  auto [server_sock, client_sock] = sms::test::MakeLoopbackPair(io);

  int closes = 0;
  auto session = std::make_shared<Session>(
      std::move(server_sock), 1,
      [](std::shared_ptr<Session>, std::string) {},
      [&](std::shared_ptr<Session>) {
        ++closes;
        io.stop();
      },
      [](std::shared_ptr<Session>, sms::common::Error) {});
  session->Start();
  session->Close();
  session->Close();

  Pump(io);

  EXPECT_EQ(1, closes);
}

TEST(SessionTest, Send_AfterClose_Noop) {
  asio::io_context io;
  auto [server_sock, client_sock] = sms::test::MakeLoopbackPair(io);

  int closes = 0;
  int errors = 0;
  auto session = std::make_shared<Session>(
      std::move(server_sock), 1,
      [](std::shared_ptr<Session>, std::string) {},
      [&](std::shared_ptr<Session>) {
        ++closes;
        io.stop();
      },
      [&](std::shared_ptr<Session>, sms::common::Error) { ++errors; });
  session->Start();
  session->Close();
  session->Send("ignored");

  Pump(io);

  EXPECT_FALSE(session->IsOpen());
  EXPECT_EQ(1, closes);
  EXPECT_EQ(0, errors);
}

TEST(SessionTest, OversizedHeader_ClosesSession) {
  asio::io_context io;
  auto [server_sock, client_sock] = sms::test::MakeLoopbackPair(io);

  int closes = 0;
  std::vector<sms::common::Error> errors;
  auto session = std::make_shared<Session>(
      std::move(server_sock), 1,
      [](std::shared_ptr<Session>, std::string) {},
      [&](std::shared_ptr<Session>) {
        ++closes;
        io.stop();
      },
      [&](std::shared_ptr<Session>, sms::common::Error error) {
        errors.push_back(error);
        io.stop();
      });
  session->Start();

  const std::uint32_t len = 128 * 1024;  // > kMaxPayloadSize (64 KiB)
  const char header[4] = {static_cast<char>((len >> 24) & 0xFF),
                          static_cast<char>((len >> 16) & 0xFF),
                          static_cast<char>((len >> 8) & 0xFF),
                          static_cast<char>(len & 0xFF)};
  asio::write(client_sock, asio::buffer(header, 4));

  Pump(io);

  EXPECT_EQ(1, closes);
  ASSERT_EQ(1u, errors.size());
  EXPECT_EQ(sms::common::ErrorCode::kProtocolError, errors[0].code);
}

TEST(SessionTest, MessageHandler_Called_PerFrame) {
  asio::io_context io;
  auto [server_sock, client_sock] = sms::test::MakeLoopbackPair(io);

  std::vector<std::string> got;
  auto session = std::make_shared<Session>(
      std::move(server_sock), 1,
      [&](std::shared_ptr<Session>, std::string payload) {
        got.push_back(std::move(payload));
        if (got.size() == 2) io.stop();
      },
      [](std::shared_ptr<Session>) {},
      [](std::shared_ptr<Session>, sms::common::Error) {});
  session->Start();

  asio::write(client_sock, asio::buffer(EncodeFrame("first") + EncodeFrame("second")));

  Pump(io);

  ASSERT_EQ(2u, got.size());
  EXPECT_EQ("first", got[0]);
  EXPECT_EQ("second", got[1]);
}

}  // namespace
}  // namespace sms::server
