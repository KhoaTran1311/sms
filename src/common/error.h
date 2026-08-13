#pragma once

#include <string>
#include <utility>

namespace sms::common {

enum class ErrorCode {
  kNone,            // success sentinel (never used as an error)
  kInvalidArgument, // caller passed a bad value
  kPayloadTooLarge, // frame exceeds kMaxPayloadSize
  kTruncatedFrame,  // partial frame, more data required
  kProtocolError,   // malformed message / violation of the wire contract
  kIoError,         // socket or file I/O failure (carries OS message)
  kDisconnected,    // peer closed or connection lost
  kConnectionRefused,
  kBindFailed, // server could not bind the listen socket
  kInternal,   // unexpected invariant violation; file bug
};

struct Error {
  ErrorCode code = ErrorCode::kInternal;
  std::string message;

  static Error Make(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
  }
};

}  // namespace sms::common
