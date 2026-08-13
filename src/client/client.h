#pragma once

#include <asio.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "common/result.h"
#include "net/frame_codec.h"

namespace sms::client {

// UI-agnostic client core: async connect, framed send, receive callback.
// The caller owns the io_context and must keep a shared_ptr to the Client
// alive while it is used; pending handlers hold their own reference.
class Client : public std::enable_shared_from_this<Client> {
 public:
  using MessageHandler = std::function<void(std::string payload)>;
  using ConnectHandler = std::function<void(sms::common::Error error)>;  // kNone on success
  using CloseHandler = std::function<void()>;

  Client(asio::io_context& io, MessageHandler on_message, CloseHandler on_close);

  // Async connect; fires on_connect with Error (kNone on success).
  void Connect(const asio::ip::tcp::endpoint& endpoint, ConnectHandler on_connect);
  // Encodes and queues a frame. Fails with kDisconnected if not connected,
  // kPayloadTooLarge if > 64 KiB. Never blocks, never throws.
  sms::common::Result<void> Send(std::string payload);
  // Clean disconnect: flush pending writes, then close.
  void Disconnect();
  bool connected() const;

 private:
  void DoReadHeader();
  void DoReadBody(std::size_t length);
  void DoWrite();
  // Idempotent teardown: cancels pending ops, closes the socket, and fires
  // on_close_ at most once. Runs on the strand.
  void CloseSocket();

  asio::ip::tcp::socket socket_;
  asio::strand<asio::ip::tcp::socket::executor_type> strand_;
  sms::net::FrameCodec codec_;
  std::array<char, sms::net::FrameCodec::kHeaderSize> header_{};
  std::string body_;
  std::deque<std::string> write_queue_;
  bool writing_ = false;
  bool connected_ = false;
  // Disconnect() was requested; drain the write queue, then close.
  bool disconnect_requested_ = false;
  // on_close_ has been fired; guards the exactly-once contract.
  bool close_notified_ = false;
  MessageHandler on_message_;
  CloseHandler on_close_;
};

}  // namespace sms::client
