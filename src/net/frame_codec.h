#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sms::net {
enum class DecodeStatus {
  Ok, // a complete frame was decoded from the front of the buffer
  NeedsMoreData, // buffer holds a partial frame (short header or short body)
  LengthExceedsMax, // header length field > kMaxPayloadSize (protocol error)
};

struct DecodedFrame {
  std::string payload; // opaque payload bytes (JSON text in v0, protobuf in v1)
  std::size_t total; // total bytes consumed for this frame (header + payload)
};

class FrameCodec {
public:
  static constexpr std::size_t HeaderSize = 4;
  static constexpr std::size_t MaxPayloadSize = 64 * 1024; // matches D1.10

  // Prepends the 4-byte big-endian length to `payload`. Returns false (and
  // leaves *out untouched) if payload.size() > kMaxPayloadSize.
  bool Encode(std::string_view payload, std::vector<std::uint8_t>* out) const;

  // Decodes at most one frame from the front of `buffer`.
  //   kOk:              *out filled; caller must erase buffer[0, total).
  //   kNeedsMoreData:   keep buffering, call again later.
  //   kLengthExceedsMax: caller treats it as a protocol violation.
  DecodeStatus Decode(std::string_view buffer, DecodedFrame* out) const;
};
} // namespace sms::net
