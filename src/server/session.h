#pragma once

#include "common/error.h"
#include "net/frame_codec.h"

#include <asio.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace sms::server {

// Per-connection async state machine (P0-08). Reads 4-byte big-endian
// length headers and exactly-L byte bodies, handing each payload to a
// message handler; serializes outbound frames through a strand-protected
// write queue; guarantees exactly-once close notification. Written
// strand-correct so a multi-threaded io_context pool (Phase 1) requires
// no changes to this class.
class Session : public std::enable_shared_from_this<Session> {
 public:
  using MessageHandler =
      std::function<void(std::shared_ptr<Session> session, std::string payload)>;
  using CloseHandler = std::function<void(std::shared_ptr<Session> session)>;
  using ErrorHandler =
      std::function<void(std::shared_ptr<Session> session, sms::common::Error error)>;

  Session(asio::ip::tcp::socket socket, std::uint64_t id,
          MessageHandler on_message, CloseHandler on_close, ErrorHandler on_error);

  void Start();                    // begins the read loop; call once
  void Send(std::string payload);  // thread-safe; preserves order
  void Close();                    // flush-then-close; idempotent
  bool IsOpen() const;

  std::uint64_t id() const { return id_; }
  asio::ip::tcp::endpoint RemoteEndpoint() const;

 private:
  void DoReadHeader();
  void DoReadBody(std::size_t length);
  void DoWrite();
  void DoClose();
  void FinalizeClose();
  void Fail(const asio::error_code& ec, const char* what);

  asio::ip::tcp::socket socket_;
  asio::strand<asio::ip::tcp::socket::executor_type> strand_;
  std::array<char, sms::net::FrameCodec::kHeaderSize> header_{};
  std::string body_;
  std::deque<std::string> write_queue_;
  bool writing_ = false;
  bool closed_ = false;
  std::uint64_t id_;
  MessageHandler on_message_;
  CloseHandler on_close_;
  ErrorHandler on_error_;
};

}  // namespace sms::server
