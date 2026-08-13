#pragma once

#include <string>
#include <utility>

namespace sms::common {
enum class ErrorCode {
  None, // success sentinel (never used as an error)
  InvalidArgument, // caller passed a bad value
  PayloadTooLarge, // frame exceeds MaxPayloadSize
  TruncatedFrame, // partial frame, more data required
  ProtocolError, // malformed message / violation of the wire contract
  IoError, // socket or file I/O failure (carries OS message)
  Disconnected, // peer closed or connection lost
  ConnectionRefused,
  BindFailed, // server could not bind the listen socket
  Internal, // unexpected invariant violation; file bug
};

struct Error {
  ErrorCode code = ErrorCode::Internal;
  std::string message;

  static Error Make(ErrorCode code, std::string message) {
    return Error{ code, std::move(message) };
  }
};
} // namespace sms::common
