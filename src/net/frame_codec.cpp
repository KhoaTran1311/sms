#include "net/frame_codec.h"

namespace sms::net {

bool FrameCodec::Encode(std::string_view payload, std::vector<std::uint8_t>* out) const {
  if (payload.size() > kMaxPayloadSize) return false;
  const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
  out->clear();
  out->reserve(kHeaderSize + payload.size());
  out->push_back(static_cast<std::uint8_t>((len >> 24) & 0xFF));
  out->push_back(static_cast<std::uint8_t>((len >> 16) & 0xFF));
  out->push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
  out->push_back(static_cast<std::uint8_t>(len & 0xFF));
  out->insert(out->end(), payload.begin(), payload.end());
  return true;
}

DecodeStatus FrameCodec::Decode(std::string_view buffer, DecodedFrame* out) const {
  if (buffer.size() < kHeaderSize) return DecodeStatus::kNeedsMoreData;
  const std::size_t len =
      (static_cast<std::uint8_t>(buffer[0]) << 24) |
      (static_cast<std::uint8_t>(buffer[1]) << 16) |
      (static_cast<std::uint8_t>(buffer[2]) << 8) |
      static_cast<std::uint8_t>(buffer[3]);
  if (len > kMaxPayloadSize) return DecodeStatus::kLengthExceedsMax;
  if (buffer.size() < kHeaderSize + len) return DecodeStatus::kNeedsMoreData;
  out->payload.assign(buffer.substr(kHeaderSize, len));
  out->total = kHeaderSize + len;
  return DecodeStatus::kOk;
}

}  // namespace sms::net