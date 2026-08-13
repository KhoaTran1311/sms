#include "net/frame_codec.h"

namespace sms::net {
bool FrameCodec::Encode(std::string_view payload, std::vector<std::uint8_t>* out) const {
  if (payload.size() > MaxPayloadSize) return false;
  const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
  out->clear();
  out->reserve(HeaderSize + payload.size());
  out->push_back(static_cast<std::uint8_t>((len >> 24) & 0xFF));
  out->push_back(static_cast<std::uint8_t>((len >> 16) & 0xFF));
  out->push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
  out->push_back(static_cast<std::uint8_t>(len & 0xFF));
  out->insert(out->end(), payload.begin(), payload.end());
  return true;
}

DecodeStatus FrameCodec::Decode(std::string_view buffer, DecodedFrame* out) const {
  if (buffer.size() < HeaderSize) return DecodeStatus::NeedsMoreData;
  const std::size_t len =
    (static_cast<std::uint8_t>(buffer[0]) << 24) |
    (static_cast<std::uint8_t>(buffer[1]) << 16) |
    (static_cast<std::uint8_t>(buffer[2]) << 8) |
    static_cast<std::uint8_t>(buffer[3]);
  if (len > MaxPayloadSize) return DecodeStatus::LengthExceedsMax;
  if (buffer.size() < HeaderSize + len) return DecodeStatus::NeedsMoreData;
  out->payload.assign(buffer.substr(HeaderSize, len));
  out->total = HeaderSize + len;
  return DecodeStatus::Ok;
}
} // namespace sms::net
