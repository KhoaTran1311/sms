#include "net/frame_codec.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace sms::net {
namespace {

std::string_view ToStringView(const std::vector<std::uint8_t>& bytes) {
  return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Deterministic seed for RoundTrip_RandomPayloads: fixed PRNG so the test is
// reproducible; the constant is the reproduction seed.
constexpr std::uint32_t kRandomSeed = 0x504F3034;

TEST(FrameCodecTest, RoundTrip_SmallJson) {
  const FrameCodec codec;
  const std::string payload = R"({"msg":"hello"})";
  std::vector<std::uint8_t> out;
  ASSERT_TRUE(codec.Encode(payload, &out));
  ASSERT_EQ(out.size(), 4 + payload.size());
  DecodedFrame frame;
  ASSERT_EQ(codec.Decode(ToStringView(out), &frame), DecodeStatus::kOk);
  EXPECT_EQ(frame.payload, payload);
  EXPECT_EQ(frame.total, 4 + payload.size());
}

TEST(FrameCodecTest, RoundTrip_EmptyPayload) {
  const FrameCodec codec;
  std::vector<std::uint8_t> out;
  ASSERT_TRUE(codec.Encode("", &out));
  ASSERT_EQ(out.size(), 4);
  DecodedFrame frame;
  ASSERT_EQ(codec.Decode(ToStringView(out), &frame), DecodeStatus::kOk);
  EXPECT_TRUE(frame.payload.empty());
  EXPECT_EQ(frame.total, 4);
}

TEST(FrameCodecTest, RoundTrip_RandomPayloads) {
  const FrameCodec codec;
  std::mt19937 rng(kRandomSeed);
  std::uniform_int_distribution<std::size_t> size_dist(0, 8192);
  std::uniform_int_distribution<int> byte_dist(0, 255);
  for (int i = 0; i < 1000; ++i) {
    const std::size_t size = size_dist(rng);
    std::string payload(size, '\0');
    for (std::size_t j = 0; j < size; ++j) {
      payload[j] = static_cast<char>(byte_dist(rng));
    }
    std::vector<std::uint8_t> out;
    ASSERT_TRUE(codec.Encode(payload, &out)) << "encode failed for payload #" << i;
    ASSERT_EQ(out.size(), 4 + size) << "payload #" << i;
    DecodedFrame frame;
    ASSERT_EQ(codec.Decode(ToStringView(out), &frame), DecodeStatus::kOk)
        << "decode failed for payload #" << i;
    EXPECT_EQ(frame.payload, payload) << "payload #" << i;
    EXPECT_EQ(frame.total, 4 + size) << "payload #" << i;
  }
}

struct DecodeCase {
  std::string name;
  std::string buffer;
  DecodeStatus expected_status;
  std::string expected_payload;
  std::size_t expected_total;
};

class FrameCodecDecodeTest : public ::testing::TestWithParam<DecodeCase> {};

TEST_P(FrameCodecDecodeTest, Decodes) {
  const FrameCodec codec;
  DecodedFrame frame;
  const DecodeStatus status = codec.Decode(GetParam().buffer, &frame);
  EXPECT_EQ(status, GetParam().expected_status);
  if (status == DecodeStatus::kOk) {
    EXPECT_EQ(frame.payload, GetParam().expected_payload);
    EXPECT_EQ(frame.total, GetParam().expected_total);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Decode, FrameCodecDecodeTest,
    ::testing::Values(
        DecodeCase{"Decode_TruncatedHeader_OneByte", std::string("\x00", 1),
                   DecodeStatus::kNeedsMoreData, "", 0},
        DecodeCase{"Decode_TruncatedHeader_ThreeBytes", std::string("\x00\x00\x05", 3),
                   DecodeStatus::kNeedsMoreData, "", 0},
        DecodeCase{"Decode_TruncatedBody",
                   std::string("\x00\x00\x00\x05" "AB", 6),
                   DecodeStatus::kNeedsMoreData, "", 0},
        DecodeCase{"Decode_ZeroLengthBody", std::string("\x00\x00\x00\x00", 4),
                   DecodeStatus::kOk, "", 4},
        DecodeCase{"Decode_OversizedLength",
                   std::string("\x00\x02\x00\x00" "junk", 8),
                   DecodeStatus::kLengthExceedsMax, "", 0},
        DecodeCase{"Decode_OversizedLength_ZeroBody",
                   std::string("\x00\x02\x00\x00", 4),
                   DecodeStatus::kLengthExceedsMax, "", 0}),
    [](const ::testing::TestParamInfo<DecodeCase>& info) { return info.param.name; });

TEST(FrameCodecTest, Decode_MaxPayloadSize_Exact) {
  const FrameCodec codec;
  const std::string body(65536, 'x');
  std::string buffer("\x00\x01\x00\x00", 4);
  buffer += body;
  DecodedFrame frame;
  ASSERT_EQ(codec.Decode(buffer, &frame), DecodeStatus::kOk);
  EXPECT_EQ(frame.payload.size(), 65536);
  EXPECT_EQ(frame.payload, body);
  EXPECT_EQ(frame.total, 4 + 65536);
}

TEST(FrameCodecTest, Encode_Rejects_Oversized) {
  const FrameCodec codec;
  const std::vector<std::uint8_t> original = {1, 2, 3, 4};
  std::vector<std::uint8_t> out = original;
  const std::string payload(65537, 'x');
  EXPECT_FALSE(codec.Encode(payload, &out));
  EXPECT_EQ(out, original);
}

TEST(FrameCodecTest, Encode_Accepts_Max) {
  const FrameCodec codec;
  const std::string payload(65536, 'x');
  std::vector<std::uint8_t> out;
  ASSERT_TRUE(codec.Encode(payload, &out));
  ASSERT_EQ(out.size(), 4 + 65536);
  EXPECT_EQ(out[0], 0x00);
  EXPECT_EQ(out[1], 0x01);
  EXPECT_EQ(out[2], 0x00);
  EXPECT_EQ(out[3], 0x00);
}

TEST(FrameCodecTest, ByteOrder_BigEndian_Vector) {
  const FrameCodec codec;
  std::vector<std::uint8_t> out;
  ASSERT_TRUE(codec.Encode("ABCD", &out));
  const std::vector<std::uint8_t> expected = {
      0x00, 0x00, 0x00, 0x04, 0x41, 0x42, 0x43, 0x44};
  EXPECT_EQ(out, expected);
}

TEST(FrameCodecTest, Decode_TwoFrames_InOneBuffer) {
  const FrameCodec codec;
  std::vector<std::uint8_t> buffer;
  std::vector<std::uint8_t> second;
  ASSERT_TRUE(codec.Encode("a", &buffer));
  ASSERT_TRUE(codec.Encode("bcd", &second));
  buffer.insert(buffer.end(), second.begin(), second.end());

  const std::string_view view = ToStringView(buffer);
  DecodedFrame first;
  ASSERT_EQ(codec.Decode(view, &first), DecodeStatus::kOk);
  EXPECT_EQ(first.payload, "a");
  EXPECT_EQ(first.total, 5);

  DecodedFrame second_frame;
  ASSERT_EQ(codec.Decode(view.substr(first.total), &second_frame), DecodeStatus::kOk);
  EXPECT_EQ(second_frame.payload, "bcd");
  EXPECT_EQ(second_frame.total, 7);
}

TEST(FrameCodecTest, Decode_SecondFrame_Truncated) {
  const FrameCodec codec;
  std::vector<std::uint8_t> buffer;
  ASSERT_TRUE(codec.Encode("hi", &buffer));
  buffer.push_back(0x00);

  const std::string_view view = ToStringView(buffer);
  DecodedFrame first;
  ASSERT_EQ(codec.Decode(view, &first), DecodeStatus::kOk);
  EXPECT_EQ(first.payload, "hi");
  EXPECT_EQ(first.total, 6);

  DecodedFrame second;
  EXPECT_EQ(codec.Decode(view.substr(first.total), &second), DecodeStatus::kNeedsMoreData);
}

TEST(FrameCodecTest, Decode_DoesNotModifyInput) {
  const FrameCodec codec;
  std::vector<std::uint8_t> frame;
  ASSERT_TRUE(codec.Encode("hi", &frame));
  frame.insert(frame.end(), {'X', 'X', 'Y', 'Y'});
  const std::string input(ToStringView(frame));
  const std::string before = input;

  DecodedFrame decoded;
  ASSERT_EQ(codec.Decode(input, &decoded), DecodeStatus::kOk);
  EXPECT_EQ(decoded.payload, "hi");
  EXPECT_EQ(decoded.total, 6);
  EXPECT_EQ(input, before);
}

TEST(FrameCodecTest, Encode_EmptyOutput_SafeReuse) {
  const FrameCodec codec;
  std::vector<std::uint8_t> out;
  ASSERT_TRUE(codec.Encode("first", &out));
  ASSERT_EQ(out.size(), 9);

  ASSERT_TRUE(codec.Encode("second", &out));
  const std::vector<std::uint8_t> expected = {
      0x00, 0x00, 0x00, 0x06, 0x73, 0x65, 0x63, 0x6F, 0x6E, 0x64};
  EXPECT_EQ(out, expected);
}

}  // namespace
}  // namespace sms::net
