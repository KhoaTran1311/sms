#include "common/log.h"
#include "net/frame_codec.h"
#include "server/server.h"
#include "support/loopback.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <asio.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// P0-11: in-process server integration tests. A real sms::server::Server
// runs on an ephemeral port in this process; tests connect raw loopback
// sockets speaking the v0 wire format via the FramedClient helper (NOT the
// sms::client::Client, which P0-14 covers). Every wait is bounded by a
// deadline timer — no sleeps — and the fixture captures SMS_LOG_* lines into
// a memory ringbuffer, dumped to stderr when a case fails.
namespace sms::server {
namespace {

using sms::test::FramedClient;
using sms::test::RecvResult;

constexpr auto kWait = std::chrono::milliseconds(5000);  // per-wait deadline
constexpr auto kQuiet = std::chrono::milliseconds(200);  // quiet-period assertion window

auto Deadline(std::chrono::milliseconds after) {
  return std::chrono::steady_clock::now() + after;
}

class ServerRelayTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Capture every SMS_LOG_* line from the server into memory so a failing
    // case can dump the server's view of events.
    logs_ = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
    auto logger = std::make_shared<spdlog::logger>("sms", logs_);
    logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%n] %v");
    logger->set_level(spdlog::level::debug);
    spdlog::drop("sms");
    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);

    server_ = std::make_shared<Server>(io_, "127.0.0.1", 0);
    ASSERT_TRUE(server_->Start().ok());
  }

  void TearDown() override {
    server_->Stop();
    io_.restart();
    if (HasFailure()) {
      std::cerr << "\n--- captured SMS_LOG lines (test failure) ---\n";
      for (const auto& line : logs_->last_formatted(512)) {
        std::cerr << line << '\n';
      }
      std::cerr << "------------------------------------------------\n";
    }
    spdlog::drop("sms");
  }

  asio::ip::tcp::endpoint Endpoint() const { return server_->LocalEndpoint(); }

  std::shared_ptr<FramedClient> Connect() {
    return std::make_shared<FramedClient>(io_, Endpoint());
  }

  // Runs io until the test's expectations settle; run_for's deadline is a
  // hang guard only — every await carries a shorter deadline of its own.
  // The server's accept loop and session reads are perpetual work, so the
  // run loop terminates on io_.stop() (Settle), never on an empty queue.
  void Pump() { sms::test::Pump(io_, kWait + std::chrono::seconds(1)); }

  // Posts an awaited frame; `handler` runs when the request settles (frame
  // delivered, deadline, or EOF/error).
  template <typename Handler>
  void AwaitOne(std::shared_ptr<FramedClient> client, std::chrono::milliseconds after,
                Handler handler) {
    ++outstanding_;
    client->AwaitPayload(Deadline(after),
                         [this, handler = std::move(handler)](RecvResult r, std::string p) mutable {
                           handler(r, std::move(p));
                           Settle();
                         });
  }

  // Polls `predicate` until true or the deadline; asserts it became true.
  void WaitUntil(std::function<bool()> predicate) {
    ++outstanding_;
    bool satisfied = false;
    sms::test::WaitFor(io_, std::move(predicate), Deadline(kWait),
                       [this, &satisfied](bool ok) {
                         satisfied = ok;
                         Settle();
                       });
    Pump();
    EXPECT_TRUE(satisfied);
  }

  // Every outstanding await/poll counts against this; when the last one
  // settles the run loop is stopped so Pump returns promptly.
  void Settle() {
    if (--outstanding_ == 0) io_.stop();
  }

  asio::io_context io_;
  std::shared_ptr<Server> server_;
  std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> logs_;
  int outstanding_ = 0;
};

// 1: A sends "hello"; B connected. B receives it; A receives nothing within
// the 200 ms quiet window (relay excludes the origin).
TEST_F(ServerRelayTest, Relay_AtoB_NotBackToA) {
  auto a = Connect();
  auto b = Connect();

  RecvResult a_result = RecvResult::kPayload;
  AwaitOne(a, kQuiet, [&](RecvResult r, std::string) { a_result = r; });

  RecvResult b_result = RecvResult::kTimeout;
  std::string b_payload;
  AwaitOne(b, kWait, [&](RecvResult r, std::string p) {
    b_result = r;
    b_payload = std::move(p);
  });

  a->SendPayload("hello");
  Pump();

  EXPECT_EQ(RecvResult::kPayload, b_result);
  EXPECT_EQ("hello", b_payload);
  EXPECT_EQ(RecvResult::kTimeout, a_result);
}

// 2: B sends "world"; A receives it (relay works in both directions).
TEST_F(ServerRelayTest, Relay_BtoA_Symmetric) {
  auto a = Connect();
  auto b = Connect();

  RecvResult a_result = RecvResult::kTimeout;
  std::string a_payload;
  AwaitOne(a, kWait, [&](RecvResult r, std::string p) {
    a_result = r;
    a_payload = std::move(p);
  });

  b->SendPayload("world");
  Pump();

  EXPECT_EQ(RecvResult::kPayload, a_result);
  EXPECT_EQ("world", a_payload);
}

// 3: A sends with B and C connected; both B and C receive; A gets nothing.
TEST_F(ServerRelayTest, Relay_ThreeClients_Broadcast) {
  auto a = Connect();
  auto b = Connect();
  auto c = Connect();
  WaitUntil([&] { return server_->SessionCount() == 3; });

  RecvResult a_result = RecvResult::kPayload;
  AwaitOne(a, kQuiet, [&](RecvResult r, std::string) { a_result = r; });

  std::string b_payload;
  AwaitOne(b, kWait, [&](RecvResult r, std::string p) {
    b_payload = std::move(p);
  });
  std::string c_payload;
  AwaitOne(c, kWait, [&](RecvResult r, std::string p) {
    c_payload = std::move(p);
  });

  a->SendPayload("broadcast");
  Pump();

  EXPECT_EQ("broadcast", b_payload);
  EXPECT_EQ("broadcast", c_payload);
  EXPECT_EQ(RecvResult::kTimeout, a_result);
}

// 4: A sends 5 frames back-to-back; B receives all 5, in order, complete.
TEST_F(ServerRelayTest, Relay_MultipleFrames_Ordered) {
  auto a = Connect();
  auto b = Connect();

  std::vector<std::string> got(5);
  std::vector<bool> ok(5, false);
  for (int i = 0; i < 5; ++i) {
    AwaitOne(b, kWait, [&, i](RecvResult r, std::string p) {
      got[i] = std::move(p);
      ok[i] = r == RecvResult::kPayload;
    });
  }

  a->SendPayload("one");
  a->SendPayload("two");
  a->SendPayload("three");
  a->SendPayload("four");
  a->SendPayload("five");
  Pump();

  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(ok[i]) << "frame " << i;
  }
  EXPECT_EQ(std::vector<std::string>({"one", "two", "three", "four", "five"}), got);
}

// 5: Session lifecycle — count follows connects and closes, down to 0.
TEST_F(ServerRelayTest, SessionLifecycle_Count) {
  auto a = Connect();
  WaitUntil([&] { return server_->SessionCount() == 1; });

  auto b = Connect();
  WaitUntil([&] { return server_->SessionCount() == 2; });

  a->Close();
  WaitUntil([&] { return server_->SessionCount() == 1; });

  b->Close();
  WaitUntil([&] { return server_->SessionCount() == 0; });
}

// 6: A closes; B sends — B gets nothing back (no peer), C still receives
// (the server survived the disconnect).
TEST_F(ServerRelayTest, Close_Then_Relay_StillWorks) {
  auto a = Connect();
  auto b = Connect();
  auto c = Connect();
  WaitUntil([&] { return server_->SessionCount() == 3; });

  a->Close();
  WaitUntil([&] { return server_->SessionCount() == 2; });

  RecvResult b_result = RecvResult::kPayload;
  AwaitOne(b, kQuiet, [&](RecvResult r, std::string) { b_result = r; });

  std::string c_payload;
  AwaitOne(c, kWait, [&](RecvResult r, std::string p) {
    c_payload = std::move(p);
  });

  b->SendPayload("after-close");
  Pump();

  EXPECT_EQ(RecvResult::kTimeout, b_result);  // no peer left to relay back to
  EXPECT_EQ("after-close", c_payload);
}

// 7: A sends a header claiming 128 KiB (> the 64 KiB codec cap) and no body;
// the server closes A's connection (EOF) and leaves B untouched.
TEST_F(ServerRelayTest, OversizedFrame_ClosesSender) {
  auto a = Connect();
  auto b = Connect();

  RecvResult a_result = RecvResult::kPayload;
  AwaitOne(a, kWait, [&](RecvResult r, std::string) { a_result = r; });

  RecvResult b_result = RecvResult::kPayload;
  AwaitOne(b, kQuiet, [&](RecvResult r, std::string) { b_result = r; });

  const std::uint32_t len = 128 * 1024;  // > kMaxPayloadSize (64 KiB)
  const char header[4] = {static_cast<char>((len >> 24) & 0xFF),
                          static_cast<char>((len >> 16) & 0xFF),
                          static_cast<char>((len >> 8) & 0xFF),
                          static_cast<char>(len & 0xFF)};
  asio::write(a->socket(), asio::buffer(header, 4));

  Pump();

  EXPECT_EQ(RecvResult::kEof, a_result);          // server closed the sender
  EXPECT_TRUE(b->IsOpen());                       // B unaffected
  EXPECT_EQ(RecvResult::kTimeout, b_result);      // and nothing was relayed
}

// 8: A sends garbage (a 4-byte header claiming a 4 GiB payload); A is closed
// as a protocol error and the server keeps accepting new connections.
TEST_F(ServerRelayTest, GarbageBytes_ClosesSender) {
  auto a = Connect();

  RecvResult a_result = RecvResult::kPayload;
  AwaitOne(a, kWait, [&](RecvResult r, std::string) { a_result = r; });

  const std::uint32_t len = 0xFFFFFFFFu;  // absurd length
  const char header[4] = {static_cast<char>((len >> 24) & 0xFF),
                          static_cast<char>((len >> 16) & 0xFF),
                          static_cast<char>((len >> 8) & 0xFF),
                          static_cast<char>(len & 0xFF)};
  asio::write(a->socket(), asio::buffer(header, 4));

  Pump();
  EXPECT_EQ(RecvResult::kEof, a_result);

  // The server must still accept and relay: fresh pair of clients.
  auto b = Connect();
  auto c = Connect();
  std::string got;
  AwaitOne(b, kWait, [&](RecvResult r, std::string p) { got = std::move(p); });
  c->SendPayload("still-works");
  Pump();

  EXPECT_EQ("still-works", got);
}

// 9: A sends while Server::Stop() runs; B receives the in-flight frame, then
// both sockets reach EOF (flush-then-close teardown).
TEST_F(ServerRelayTest, Stop_Flushes_And_Closes) {
  auto a = Connect();
  auto b = Connect();
  WaitUntil([&] { return server_->SessionCount() == 2; });

  RecvResult b_payload_result = RecvResult::kTimeout;
  std::string b_payload;
  b->AwaitPayload(Deadline(kWait), [&](RecvResult r, std::string p) {
    b_payload_result = r;
    b_payload = std::move(p);
  });

  RecvResult b_eof = RecvResult::kPayload;
  b->AwaitPayload(Deadline(kWait), [&](RecvResult r, std::string) { b_eof = r; });

  RecvResult a_eof = RecvResult::kPayload;
  a->AwaitPayload(Deadline(kWait), [&](RecvResult r, std::string) { a_eof = r; });

  a->SendPayload("in-flight");

  // Let the server read and relay the frame so it is genuinely in flight,
  // then stop: the queued write must be flushed, not dropped.
  io_.run_for(std::chrono::milliseconds(100));
  io_.restart();
  server_->Stop();
  Pump();

  EXPECT_EQ(RecvResult::kPayload, b_payload_result);
  EXPECT_EQ("in-flight", b_payload);
  EXPECT_EQ(RecvResult::kEof, b_eof);
  EXPECT_EQ(RecvResult::kEof, a_eof);
}

// 10: A and B send concurrently (two independent async chains); C receives
// exactly the two frames, each well-formed per the codec — no interleaving.
TEST_F(ServerRelayTest, ConcurrentSends_NoInterleave) {
  auto a = Connect();
  auto b = Connect();
  auto c = Connect();
  WaitUntil([&] { return server_->SessionCount() == 3; });

  std::vector<std::string> got(2);
  std::vector<bool> ok(2, false);
  for (int i = 0; i < 2; ++i) {
    AwaitOne(c, kWait, [&, i](RecvResult r, std::string p) {
      got[i] = std::move(p);
      ok[i] = r == RecvResult::kPayload;
    });
  }

  a->SendPayload("from-a");
  b->SendPayload("from-b");
  Pump();

  EXPECT_TRUE(ok[0] && ok[1]);
  std::sort(got.begin(), got.end());
  EXPECT_EQ(std::vector<std::string>({"from-a", "from-b"}), got);
}

// 11: after the server stops, a fresh server on a new ephemeral port relays
// again (PoC has no persistence — a new server starts empty by design).
TEST_F(ServerRelayTest, Reconnect_After_ServerStop_NewServer) {
  auto a = Connect();
  WaitUntil([&] { return server_->SessionCount() == 1; });

  server_->Stop();
  Pump();  // drain the flush/close handlers of the old server

  auto new_server = std::make_shared<Server>(io_, "127.0.0.1", 0);
  ASSERT_TRUE(new_server->Start().ok());

  auto x = std::make_shared<FramedClient>(io_, new_server->LocalEndpoint());
  auto y = std::make_shared<FramedClient>(io_, new_server->LocalEndpoint());
  WaitUntil([&] { return new_server->SessionCount() == 2; });

  std::string got;
  AwaitOne(y, kWait, [&](RecvResult r, std::string p) { got = std::move(p); });
  x->SendPayload("fresh");
  Pump();

  EXPECT_EQ("fresh", got);
  new_server->Stop();
}

}  // namespace
}  // namespace sms::server
