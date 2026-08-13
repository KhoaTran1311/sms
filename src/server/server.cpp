#include "server/server.h"

#include "common/log.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace sms::server {

Server::Server(asio::io_context& io, std::string host, std::uint16_t port)
    : io_(io), acceptor_(io), host_(std::move(host)), port_(port) {}

sms::common::Result<void> Server::Start() {
  asio::error_code ec;
  const auto addr = asio::ip::make_address(host_, ec);  // host_ may be "127.0.0.1"
  if (ec) {
    return sms::common::Result<void>::Err(sms::common::Error::Make(sms::common::ErrorCode::kInvalidArgument,
                                    "bad host: " + host_));
  }
  acceptor_.open(asio::ip::tcp::v4(), ec);
  acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
  acceptor_.bind(asio::ip::tcp::endpoint(addr, port_), ec);
  if (ec) {
    return sms::common::Result<void>::Err(sms::common::Error::Make(sms::common::ErrorCode::kBindFailed, ec.message()));
  }
  acceptor_.listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    return sms::common::Result<void>::Err(sms::common::Error::Make(sms::common::ErrorCode::kIoError, ec.message()));
  }
  DoAccept();
  return sms::common::Result<void>::Ok();
}

void Server::Stop() {
  asio::error_code ec;
  acceptor_.close(ec);  // stops the accept loop (pending accept cancels)
  for (auto& session : sessions_) {
    session->Close();  // flush-then-close; callbacks erase from the registry
  }
}

asio::ip::tcp::endpoint Server::LocalEndpoint() const {
  asio::error_code ec;
  const asio::ip::tcp::endpoint ep = acceptor_.local_endpoint(ec);
  return ec ? asio::ip::tcp::endpoint() : ep;
}

std::size_t Server::SessionCount() const {
  return sessions_.size();
}

void Server::DoAccept() {
  acceptor_.async_accept([self = shared_from_this()](const asio::error_code& ec,
                                                     asio::ip::tcp::socket socket) {
    if (ec) {
      // Cancelled by Stop(): the accept loop ends here.
      if (ec == asio::error::operation_aborted) return;
      SMS_LOG_WARN("accept failed: {}", ec.message());
      // Re-arm with a delay to avoid a spin on persistent errors (e.g. EMFILE).
      auto timer = std::make_shared<asio::steady_timer>(self->io_);
      timer->expires_after(std::chrono::milliseconds(100));
      timer->async_wait([self, timer](const asio::error_code&) { self->DoAccept(); });
      return;
    }
    auto session = std::make_shared<Session>(
        std::move(socket), self->next_session_id_++,
        /* on_message */ [self](auto from, std::string payload) {
          self->OnMessage(from, std::move(payload));
        },
        /* on_close */ [self](auto session) { self->OnClose(session); },
        /* on_error */ [](auto, sms::common::Error err) {
          SMS_LOG_WARN("session error: {}", err.message);
        });
    SMS_LOG_INFO("session {} connected from {}", session->id(),
                 session->RemoteEndpoint().address().to_string());
    self->sessions_.insert(session);
    session->Start();
    self->DoAccept();
  });
}

void Server::OnMessage(std::shared_ptr<Session> from, std::string payload) {
  SMS_LOG_DEBUG("relaying {} bytes from session {} to {} peer(s)", payload.size(),
                from->id(), sessions_.size() - 1);
  for (const auto& session : sessions_) {
    if (session == from) continue;  // relay excludes origin
    session->Send(payload);
  }
}

void Server::OnClose(std::shared_ptr<Session> session) {
  sessions_.erase(session);
  SMS_LOG_INFO("session {} disconnected ({} remaining)", session->id(),
               sessions_.size());
}

}  // namespace sms::server
