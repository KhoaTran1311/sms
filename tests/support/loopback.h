#pragma once

#include <asio.hpp>

#include <stdexcept>
#include <system_error>
#include <utility>

// Shared test helper (P0-08): creates a real TCP socket pair over loopback
// for in-process tests that must exercise raw wire behavior (P0-08 session
// tests, later P0-11/P0-14 integration tests).
//
// Binds an acceptor to 127.0.0.1 on an ephemeral port (port 0), connects a
// client socket to it, and accepts the server side. Both sockets live on the
// caller's io_context; synchronous connect/accept are used so the helper
// works before the io_context is running. The pair is returned as
// (server-side socket, client-side socket); the caller owns both.
//
// Throws std::system_error on setup failure (a test-harness error, not a
// runtime path the helper is meant to exercise).
namespace sms::test {

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

}  // namespace sms::test