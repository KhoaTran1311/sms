#include "client/client.h"

#include <asio.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sms::client {
namespace {

using sms::common::Error;
using sms::common::ErrorCode;

// Single-threaded io_context driver: runs handlers until `done` is true, no
// work remains, or the safety guard trips. All tests run on one thread, so
// handler state is only touched here.
void RunUntil(const std::function<bool()>& done, asio::io_context& io) {
  int guard = 0;
  while (!done()) {
    if (io.run_one() == 0 || ++guard > 100000) break;
  }
}

// Ephemeral-loopback-listener fixture. Local to this file on purpose: the
// canonical helper lives in tests/support/loopback.h (owned by P0-08) and
// should be migrated there when the branches merge.
class LoopbackListener {
 public:
  LoopbackListener(asio::io_context& io, asio::ip::tcp::endpoint* endpoint)
      : acceptor_(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0)) {
    *endpoint = acceptor_.local_endpoint();
  }

  void Accept(asio::ip::tcp::socket& peer, std::function<void()> on_accept) {
    acceptor_.async_accept(peer, [on_accept = std::move(on_accept)](
                                     const asio::error_code& ec) {
      if (!ec) on_accept();
    });
  }

 private:
  asio::ip::tcp::acceptor acceptor_;
};

TEST(ClientTest, Send_NotConnected_ReturnsError) {
  asio::io_context io;
  auto client =
      std::make_shared<Client>(io, [](std::string) {}, [] {});

  sms::common::Result<void> res = client->Send("abc");

  ASSERT_FALSE(res.ok());
  EXPECT_EQ(ErrorCode::kDisconnected, res.error().code);
}

TEST(ClientTest, Send_Oversized_ReturnsError) {
  asio::io_context io;
  auto client =
      std::make_shared<Client>(io, [](std::string) {}, [] {});

  std::string big(64 * 1024 + 1, 'x');
  sms::common::Result<void> res = client->Send(std::move(big));

  ASSERT_FALSE(res.ok());
  EXPECT_EQ(ErrorCode::kPayloadTooLarge, res.error().code);
}

TEST(ClientTest, Send_Encodes_Correctly) {
  asio::io_context io;
  asio::ip::tcp::endpoint endpoint;
  LoopbackListener listener(io, &endpoint);
  asio::ip::tcp::socket peer(io);
  bool accepted = false;
  listener.Accept(peer, [&] { accepted = true; });

  ErrorCode connect_code = ErrorCode::kInternal;
  auto client =
      std::make_shared<Client>(io, [](std::string) {}, [] {});
  client->Connect(endpoint, [&](Error e) { connect_code = e.code; });
  RunUntil([&] { return connect_code != ErrorCode::kInternal; }, io);
  ASSERT_EQ(ErrorCode::kNone, connect_code);
  RunUntil([&] { return accepted; }, io);

  sms::common::Result<void> res = client->Send("abc");
  ASSERT_TRUE(res.ok());

  std::array<char, 7> wire{};
  bool read_done = false;
  peer.async_read_some(asio::buffer(wire),
                       [&](const asio::error_code& ec, std::size_t n) {
                         read_done = true;
                         ASSERT_FALSE(ec);
                         EXPECT_EQ(7u, n);
                       });
  RunUntil([&] { return read_done; }, io);

  const std::array<char, 7> expected = {0, 0, 0, 3, 'a', 'b', 'c'};
  EXPECT_EQ(expected, wire);
}

TEST(ClientTest, OnClose_FiresOnce_OnPeerClose) {
  asio::io_context io;
  asio::ip::tcp::endpoint endpoint;
  LoopbackListener listener(io, &endpoint);
  asio::ip::tcp::socket peer(io);
  bool accepted = false;
  listener.Accept(peer, [&] { accepted = true; });

  ErrorCode connect_code = ErrorCode::kInternal;
  int close_count = 0;
  auto client = std::make_shared<Client>(
      io, [](std::string) {}, [&] { ++close_count; });
  client->Connect(endpoint, [&](Error e) { connect_code = e.code; });
  RunUntil([&] { return connect_code != ErrorCode::kInternal && accepted; },
           io);
  ASSERT_EQ(ErrorCode::kNone, connect_code);
  ASSERT_TRUE(client->connected());

  peer.close();
  RunUntil([&] { return close_count >= 1; }, io);
  EXPECT_EQ(1, close_count);
  EXPECT_FALSE(client->connected());

  for (int i = 0; i < 5; ++i) io.poll_one();
  EXPECT_EQ(1, close_count);
}

TEST(ClientTest, Connect_Refused_ReportsError) {
  asio::io_context io;
  asio::ip::tcp::endpoint endpoint;
  {
    LoopbackListener listener(io, &endpoint);
  }  // listener closed; nothing listens on the port anymore

  ErrorCode connect_code = ErrorCode::kInternal;
  auto client =
      std::make_shared<Client>(io, [](std::string) {}, [] {});
  client->Connect(endpoint, [&](Error e) { connect_code = e.code; });
  RunUntil([&] { return connect_code != ErrorCode::kInternal; }, io);

  EXPECT_EQ(ErrorCode::kConnectionRefused, connect_code);
  EXPECT_FALSE(client->connected());
}

TEST(ClientTest, Message_RoundTrip_ThroughLoopback) {
  asio::io_context io;
  asio::ip::tcp::endpoint endpoint;
  LoopbackListener listener(io, &endpoint);
  asio::ip::tcp::socket peer_a(io);
  asio::ip::tcp::socket peer_b(io);
  bool accepted_a = false;
  bool accepted_b = false;
  listener.Accept(peer_a, [&] { accepted_a = true; });
  listener.Accept(peer_b, [&] { accepted_b = true; });

  std::vector<std::string> received_a;
  std::vector<std::string> received_b;
  auto client_a = std::make_shared<Client>(
      io, [&](std::string p) { received_a.push_back(std::move(p)); }, [] {});
  auto client_b = std::make_shared<Client>(
      io, [&](std::string p) { received_b.push_back(std::move(p)); }, [] {});

  ErrorCode code_a = ErrorCode::kInternal;
  ErrorCode code_b = ErrorCode::kInternal;
  client_a->Connect(endpoint, [&](Error e) { code_a = e.code; });
  client_b->Connect(endpoint, [&](Error e) { code_b = e.code; });
  RunUntil([&] {
    return code_a != ErrorCode::kInternal && code_b != ErrorCode::kInternal &&
           accepted_a && accepted_b;
  }, io);
  ASSERT_EQ(ErrorCode::kNone, code_a);
  ASSERT_EQ(ErrorCode::kNone, code_b);

  // Echo pump: forward bytes from one peer socket to the other and back.
  // Each read is copied into a shared string first so the buffer outlives
  // the read handler; the same ownership pattern as Client::DoWrite.
  std::array<char, 4096> buf_a{};
  std::array<char, 4096> buf_b{};
  std::function<void(asio::ip::tcp::socket*, asio::ip::tcp::socket*,
                     std::array<char, 4096>&)> pump;
  pump = [&](asio::ip::tcp::socket* from, asio::ip::tcp::socket* to,
             std::array<char, 4096>& buf) {
    from->async_read_some(
        asio::buffer(buf),
        [&, from = from, to = to, buf = &buf](const asio::error_code& ec,
                                              std::size_t n) {
          if (ec) return;
          auto data = std::make_shared<std::string>(buf->data(), n);
          asio::async_write(*to, asio::buffer(*data),
                            [&, from = from, to = to, buf = buf,
                             data = std::move(data)](const asio::error_code& ec,
                                                     std::size_t) {
                              if (ec) return;
                              pump(from, to, *buf);
                            });
        });
  };
  sms::common::Result<void> sa = client_a->Send("hello from A");
  sms::common::Result<void> sb = client_b->Send("reply from B");
  ASSERT_TRUE(sa.ok());
  ASSERT_TRUE(sb.ok());
  pump(&peer_a, &peer_b, buf_a);
  pump(&peer_b, &peer_a, buf_b);

  RunUntil([&] { return !received_a.empty() && !received_b.empty(); }, io);
  EXPECT_EQ("reply from B", received_a.front());
  EXPECT_EQ("hello from A", received_b.front());
}

}  // namespace
}  // namespace sms::client
