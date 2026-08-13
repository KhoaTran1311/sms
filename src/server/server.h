#pragma once

#include "common/result.h"
#include "server/session.h"

#include <asio.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

namespace sms::server {

// Accept loop, session registry, and single-room relay (P0-09). Owns every
// Session (shared_ptr registry keyed by identity); relays every received
// payload to all sessions except the origin; torn down deterministically by
// Stop() (acceptor closed first, then every session flushed and closed).
// All state is only touched from handlers on the single io_context thread.
class Server : public std::enable_shared_from_this<Server> {
 public:
  Server(asio::io_context& io, std::string host, std::uint16_t port);

  // Binds, listens, and starts accepting. Returns the bind error mapped to
  // sms::common::Error (e.g. kBindFailed on address-in-use). Port 0 binds an
  // ephemeral port, retrievable via LocalEndpoint().
  sms::common::Result<void> Start();
  // Closes the acceptor and all sessions (flushing pending writes).
  void Stop();

  asio::ip::tcp::endpoint LocalEndpoint() const;  // real port when port==0
  std::size_t SessionCount() const;               // test hook

 private:
  void DoAccept();
  void OnMessage(std::shared_ptr<Session> from, std::string payload);
  void OnClose(std::shared_ptr<Session> session);

  asio::io_context& io_;
  asio::ip::tcp::acceptor acceptor_;
  std::string host_;
  std::uint16_t port_;
  std::unordered_set<std::shared_ptr<Session>> sessions_;
  std::uint64_t next_session_id_ = 1;
};

}  // namespace sms::server
