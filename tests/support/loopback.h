#pragma once

#include "net/frame_codec.h"

#include <asio.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// Shared test helpers (P0-08, P0-11): raw TCP loopback connections for
// in-process tests that must exercise real wire behavior.
//
// Two levels of helper:
//   * MakeLoopbackPair (P0-08): one connected socket pair, no framing —
//     used by the P0-08 session unit tests.
//   * FramedClient / ConnectFramed (P0-11): a loopback socket speaking the
//     v0 wire format via FrameCodec, with deadline-bounded async awaits —
//     used by the P0-11 server integration tests (and later P0-14).
//
// Everything is deadline-driven — no sleeps — so a missed event fails a
// test instead of hanging CI.
namespace sms::test {

// Creates a real TCP socket pair over loopback (127.0.0.1, ephemeral port).
// Returns (server-side socket, client-side socket); synchronous connect and
// accept so it works before the io_context is running.
inline std::pair<asio::ip::tcp::socket, asio::ip::tcp::socket> MakeLoopbackPair(
    asio::io_context& io) {
  asio::ip::tcp::acceptor acceptor(
      io, asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));

  asio::ip::tcp::socket client(io);
  asio::error_code ec;
  client.connect(acceptor.local_endpoint(), ec);
  if (ec) throw std::system_error(ec);

  asio::ip::tcp::socket server = acceptor.accept(ec);
  if (ec) throw std::system_error(ec);

  return {std::move(server), std::move(client)};
}

// Outcome of one FramedClient::AwaitPayload request.
enum class RecvResult {
  kPayload,  // a complete, codec-valid frame was decoded and delivered
  kTimeout,  // the await's deadline expired before a frame arrived
  kEof,      // the peer closed the connection (clean EOF)
  kError,    // transport or protocol error
};

// A loopback TCP client that speaks the v0 wire format (4-byte big-endian
// length header + payload) through a FrameCodec. Reads and writes are async
// on the caller's io_context; AwaitPayload requests are queued and served in
// order by a single internal read loop, each bounded by its own deadline.
class FramedClient {
 public:
  using Deadline = std::chrono::steady_clock::time_point;

  // Synchronously connects to `endpoint`; throws std::system_error on
  // connection failure (a test-harness error, not a path under test).
  FramedClient(asio::io_context& io, asio::ip::tcp::endpoint endpoint)
      : io_(io), socket_(io) {
    asio::error_code ec;
    socket_.connect(endpoint, ec);
    if (ec) throw std::system_error(ec);
  }

  // Encodes `payload` as one frame and posts an async write. Throws
  // std::runtime_error if the payload exceeds FrameCodec::kMaxPayloadSize.
  void SendPayload(std::string_view payload) {
    auto wire = std::make_shared<std::vector<std::uint8_t>>();
    if (!codec_.Encode(payload, wire.get())) {
      throw std::runtime_error("SendPayload: payload exceeds kMaxPayloadSize");
    }
    asio::async_write(socket_, asio::buffer(*wire),
                      [wire](const asio::error_code&, std::size_t) {});
  }

  // Queues a receive request for the next complete frame. `handler` is
  // invoked with (RecvResult, payload) exactly once: kPayload when a frame
  // arrives, kTimeout when the deadline expires first, kEof on clean peer
  // close, kError otherwise. Requests are served strictly in queue order.
  void AwaitPayload(Deadline deadline,
                    std::function<void(RecvResult, std::string)> handler) {
    auto req = std::make_shared<ReadRequest>();
    req->deadline = deadline;
    req->handler = std::move(handler);
    req->timer = std::make_shared<asio::steady_timer>(io_);
    req->timer->expires_at(deadline);
    req->timer->async_wait([this, req](const asio::error_code& ec) {
      if (ec || req->done) return;  // cancelled or already satisfied
      req->done = true;
      if (!pending_.empty() && pending_.front() == req) {
        socket_.cancel();  // abort the in-flight read; its handler finishes this request
      }
    });
    pending_.push_back(std::move(req));
    if (!reading_) ReadFrontHeader();
  }

  // Closes the socket (shutdown both directions). Pending reads, if any,
  // complete with RecvResult::kError.
  void Close() {
    asio::error_code ec;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
  }

  asio::ip::tcp::socket& socket() { return socket_; }
  bool IsOpen() const { return socket_.is_open(); }

 private:
  struct ReadRequest {
    Deadline deadline;
    std::function<void(RecvResult, std::string)> handler;
    std::shared_ptr<asio::steady_timer> timer;
    bool done = false;
  };

  std::size_t DecodeLength() const {
    return (static_cast<std::uint8_t>(header_[0]) << 24) |
           (static_cast<std::uint8_t>(header_[1]) << 16) |
           (static_cast<std::uint8_t>(header_[2]) << 8) |
           static_cast<std::uint8_t>(header_[3]);
  }

  // Serves the front request: skips any that expired while queued, starts a
  // header+body read for the first live one, or stops the loop when the
  // queue is empty.
  void ReadFrontHeader() {
    while (!pending_.empty() && pending_.front()->done) {
      auto req = std::move(pending_.front());
      pending_.pop_front();
      req->timer->cancel();
      req->handler(RecvResult::kTimeout, {});
    }
    if (pending_.empty()) {
      reading_ = false;
      return;
    }
    reading_ = true;
    auto req = pending_.front();
    asio::async_read(socket_, asio::buffer(header_),
                     asio::transfer_exactly(sms::net::FrameCodec::kHeaderSize),
                     [this, req](const asio::error_code& ec, std::size_t) {
                       if (ec) return Finish(req, ec);
                       if (req->done) return ReadFrontHeader();
                       const std::size_t len = DecodeLength();
                       if (len > sms::net::FrameCodec::kMaxPayloadSize) {
                         return Finish(req, asio::error::fault);
                       }
                       body_.resize(len);
                       asio::async_read(socket_, asio::buffer(body_),
                                        asio::transfer_exactly(len),
                                        [this, req](const asio::error_code& ec2,
                                                    std::size_t) {
                                          if (ec2) return Finish(req, ec2);
                                          if (req->done) return ReadFrontHeader();
                                          req->done = true;
                                          req->timer->cancel();
                                          pending_.pop_front();
                                          req->handler(RecvResult::kPayload,
                                                       std::move(body_));
                                          ReadFrontHeader();
                                        });
                     });
  }

  // Completes the front request with the result mapped from `ec` and
  // advances the loop. EOF is delivered to each subsequent queued request
  // in turn (their reads also observe the closed connection).
  void Finish(std::shared_ptr<ReadRequest> req, const asio::error_code& ec) {
    req->done = true;
    req->timer->cancel();
    pending_.pop_front();
    RecvResult result;
    if (ec == asio::error::eof) {
      result = RecvResult::kEof;
    } else if (ec == asio::error::operation_aborted) {
      result = RecvResult::kTimeout;  // our deadline timer cancelled the read
    } else {
      result = RecvResult::kError;
    }
    req->handler(result, {});
    ReadFrontHeader();
  }

  asio::io_context& io_;
  asio::ip::tcp::socket socket_;
  sms::net::FrameCodec codec_;
  std::array<char, sms::net::FrameCodec::kHeaderSize> header_{};
  std::string body_;
  std::deque<std::shared_ptr<ReadRequest>> pending_;
  bool reading_ = false;
};

// Connects a FramedClient to `endpoint` (convenience wrapper; may throw
// std::system_error on connection failure).
inline FramedClient ConnectFramed(asio::io_context& io,
                                  const asio::ip::tcp::endpoint& endpoint) {
  return FramedClient(io, endpoint);
}

// Polls `predicate` every 10 ms until it returns true (handler(true)) or
// `deadline` passes (handler(false)). Purely timer-driven — no sleeps.
inline void WaitFor(asio::io_context& io, std::function<bool()> predicate,
                    std::chrono::steady_clock::time_point deadline,
                    std::function<void(bool)> handler) {
  auto state = std::make_shared<asio::steady_timer>(io);
  auto tick = std::make_shared<std::function<void(const asio::error_code&)>>();
  *tick = [state, tick, predicate = std::move(predicate), deadline,
           handler = std::move(handler)](const asio::error_code& ec) {
    if (ec) return;
    if (predicate()) {
      handler(true);
      return;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      handler(false);
      return;
    }
    state->expires_after(std::chrono::milliseconds(10));
    state->async_wait(*tick);
  };
  (*tick)(asio::error_code());  // first poll immediately
}

// Runs `io` until all work completes or the watchdog fires, then resets it
// for reuse. The watchdog is a last-resort hang guard: every test wait must
// already have its own (shorter) deadline.
inline void Pump(asio::io_context& io,
                 std::chrono::milliseconds wait = std::chrono::milliseconds(6000)) {
  asio::steady_timer watchdog(io, wait);
  watchdog.async_wait([&io](const asio::error_code&) { io.stop(); });
  io.run();
  io.restart();
}

}  // namespace sms::test
