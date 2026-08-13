#include "client/client.h"

#include <asio.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace sms::client {

Client::Client(asio::io_context& io, MessageHandler on_message, CloseHandler on_close)
    : socket_(io),
      strand_(asio::make_strand(io)),
      on_message_(std::move(on_message)),
      on_close_(std::move(on_close)) {}

void Client::Connect(const asio::ip::tcp::endpoint& endpoint, ConnectHandler on_connect) {
  auto self = shared_from_this();
  socket_.async_connect(endpoint, [self, on_connect = std::move(on_connect)](
                                      const asio::error_code& ec) mutable {
    if (ec) {
      on_connect(sms::common::Error::Make(
          ec == asio::error::connection_refused ? sms::common::ErrorCode::kConnectionRefused
                                                : sms::common::ErrorCode::kIoError,
          ec.message()));
      return;
    }
    self->connected_ = true;
    on_connect(sms::common::Error::Make(sms::common::ErrorCode::kNone, ""));
    self->DoReadHeader();
  });
}

sms::common::Result<void> Client::Send(std::string payload) {
  std::vector<std::uint8_t> frame;
  if (!codec_.Encode(payload, &frame)) {
    return sms::common::Result<void>::Err(sms::common::Error::Make(
        sms::common::ErrorCode::kPayloadTooLarge, "payload exceeds 64 KiB"));
  }
  if (!connected_) {
    return sms::common::Result<void>::Err(sms::common::Error::Make(
        sms::common::ErrorCode::kDisconnected, "not connected"));
  }
  std::string wire(frame.begin(), frame.end());
  auto self = shared_from_this();
  asio::post(strand_, [self, wire = std::move(wire)]() mutable {
    self->write_queue_.push_back(std::move(wire));
    self->DoWrite();
  });
  return sms::common::Result<void>::Ok();
}

void Client::Disconnect() {
  if (!connected_) return;
  connected_ = false;
  auto self = shared_from_this();
  asio::post(strand_, [self] {
    self->disconnect_requested_ = true;
    if (!self->writing_ && self->write_queue_.empty()) {
      self->CloseSocket();
    }
  });
}

bool Client::connected() const { return connected_; }

void Client::DoReadHeader() {
  auto self = shared_from_this();
  asio::async_read(socket_, asio::buffer(header_),
                   asio::bind_executor(strand_, [self](const asio::error_code& ec,
                                                       std::size_t) {
                     if (ec) {
                       self->CloseSocket();
                       return;
                     }
                     const std::size_t length =
                         (static_cast<std::uint8_t>(self->header_[0]) << 24) |
                         (static_cast<std::uint8_t>(self->header_[1]) << 16) |
                         (static_cast<std::uint8_t>(self->header_[2]) << 8) |
                         static_cast<std::uint8_t>(self->header_[3]);
                     if (length > sms::net::FrameCodec::kMaxPayloadSize) {
                       self->CloseSocket();
                       return;
                     }
                     self->DoReadBody(length);
                   }));
}

void Client::DoReadBody(std::size_t length) {
  auto self = shared_from_this();
  self->body_.resize(length);
  asio::async_read(socket_, asio::buffer(body_),
                   asio::bind_executor(strand_, [self](const asio::error_code& ec,
                                                       std::size_t) {
                     if (ec) {
                       self->CloseSocket();
                       return;
                     }
                      std::string frame;
                      frame.reserve(sms::net::FrameCodec::kHeaderSize + self->body_.size());
                      frame.append(self->header_.data(), sms::net::FrameCodec::kHeaderSize);
                      frame.append(self->body_);
                      sms::net::DecodedFrame decoded;
                      if (self->codec_.Decode(frame, &decoded) != sms::net::DecodeStatus::kOk) {
                        self->CloseSocket();
                        return;
                      }
                     self->on_message_(std::move(decoded.payload));
                     self->DoReadHeader();
                   }));
}

void Client::DoWrite() {
  if (writing_) return;
  if (write_queue_.empty()) {
    if (disconnect_requested_) CloseSocket();
    return;
  }
  writing_ = true;
  auto self = shared_from_this();
  std::string frame = std::move(write_queue_.front());
  write_queue_.pop_front();
  // The buffer must cover the frame for the whole write. A move into the
  // completion handler would leave the buffer dangling for small frames
  // (SSO: the move copies the inline storage, and the original dies when this
  // function returns), so the completion handler owns the frame and the
  // buffer is built from that same owned object.
  auto owned = std::make_shared<std::string>(std::move(frame));
  asio::async_write(socket_, asio::buffer(*owned),
                    asio::bind_executor(strand_, [self, owned = std::move(owned)](
                                                     const asio::error_code& ec,
                                                     std::size_t) {
                      if (ec) {
                        self->CloseSocket();
                        return;
                      }
                      self->writing_ = false;
                      self->DoWrite();
                    }));
}

void Client::CloseSocket() {
  if (close_notified_) return;
  close_notified_ = true;
  connected_ = false;
  disconnect_requested_ = false;
  asio::error_code ignored;
  socket_.cancel(ignored);
  socket_.close(ignored);
  on_close_();
}

}  // namespace sms::client
