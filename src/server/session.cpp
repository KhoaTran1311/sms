#include "server/session.h"

namespace sms::server {
namespace {

// Maps an asio error_code to the library error convention (P0-05).
sms::common::ErrorCode MapCode(const asio::error_code& ec) {
  if (ec == asio::error::fault) return sms::common::ErrorCode::kProtocolError;
  if (ec == asio::error::eof || ec == asio::error::connection_reset ||
      ec == asio::error::connection_aborted) {
    return sms::common::ErrorCode::kDisconnected;
  }
  return sms::common::ErrorCode::kIoError;
}

// Big-endian 4-byte header as in FrameCodec::Decode.
std::size_t DecodeLength(const std::array<char, sms::net::FrameCodec::kHeaderSize>& header) {
  return (static_cast<std::uint8_t>(header[0]) << 24) |
         (static_cast<std::uint8_t>(header[1]) << 16) |
         (static_cast<std::uint8_t>(header[2]) << 8) |
         static_cast<std::uint8_t>(header[3]);
}

}  // namespace

Session::Session(asio::ip::tcp::socket socket, std::uint64_t id,
                 MessageHandler on_message, CloseHandler on_close,
                 ErrorHandler on_error)
    : socket_(std::move(socket)),
      strand_(socket_.get_executor()),
      id_(id),
      on_message_(std::move(on_message)),
      on_close_(std::move(on_close)),
      on_error_(std::move(on_error)) {}

void Session::Start() {
  DoReadHeader();
}

void Session::DoReadHeader() {
  auto self = shared_from_this();
  asio::async_read(socket_, asio::buffer(header_),
                   asio::bind_executor(strand_, [self](const asio::error_code& ec,
                                                       std::size_t) {
                     if (ec) return self->Fail(ec, "read header");
                     if (self->closed_) return;
                     const std::size_t len = DecodeLength(self->header_);
                     if (len > sms::net::FrameCodec::kMaxPayloadSize) {
                       return self->Fail(asio::error::fault, "oversized frame");
                     }
                     self->DoReadBody(len);
                   }));
}

void Session::DoReadBody(std::size_t length) {
  auto self = shared_from_this();
  body_.resize(length);
  asio::async_read(socket_, asio::buffer(body_),
                   asio::bind_executor(strand_, [self](const asio::error_code& ec,
                                                       std::size_t) {
                     if (ec) return self->Fail(ec, "read body");
                     if (self->closed_) return;
                     try {
                       self->on_message_(self, std::move(self->body_));
                     } catch (...) {
                     }
                     self->body_.clear();
                     self->DoReadHeader();
                   }));
}

void Session::Send(std::string payload) {
  asio::post(strand_, [self = shared_from_this(), payload = std::move(payload)]() mutable {
    if (self->closed_) return;
    self->write_queue_.push_back(std::move(payload));
    if (!self->writing_) self->DoWrite();
  });
}

void Session::DoWrite() {
  writing_ = true;
  auto self = shared_from_this();
  asio::async_write(socket_, asio::buffer(write_queue_.front()),
                    asio::bind_executor(strand_, [self](const asio::error_code& ec,
                                                        std::size_t) {
                      if (ec) return self->Fail(ec, "write");
                      self->write_queue_.pop_front();
                      if (!self->write_queue_.empty()) {
                        self->DoWrite();
                      } else {
                        self->writing_ = false;
                        if (self->closed_) self->FinalizeClose();
                      }
                    }));
}

void Session::Close() {
  asio::post(strand_, [self = shared_from_this()]() { self->DoClose(); });
}

void Session::DoClose() {
  if (closed_) return;
  closed_ = true;
  if (writing_) return;  // flush in progress; FinalizeClose once the queue drains
  if (!write_queue_.empty()) {
    DoWrite();
    return;
  }
  FinalizeClose();
}

void Session::FinalizeClose() {
  asio::error_code ignored;
  socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
  socket_.close(ignored);
  try {
    on_close_(shared_from_this());
  } catch (...) {
  }
}

void Session::Fail(const asio::error_code& ec, const char* what) {
  if (closed_) return;
  on_error_(shared_from_this(),
            sms::common::Error::Make(MapCode(ec), std::string(what) + ": " + ec.message()));
  DoClose();
}

bool Session::IsOpen() const {
  return !closed_;
}

asio::ip::tcp::endpoint Session::RemoteEndpoint() const {
  asio::error_code ec;
  const asio::ip::tcp::endpoint ep = socket_.remote_endpoint(ec);
  return ec ? asio::ip::tcp::endpoint() : ep;
}

}  // namespace sms::server
