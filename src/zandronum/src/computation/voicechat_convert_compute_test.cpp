// [rc4l] Tests for the voice-chat float<->bytes little-endian conversions.
#include "gtest/gtest.h"
#include "computation/voicechat_convert_compute.h"

namespace
{
// [rc4l] Round-tripping a float through bytes and back yields the same value.
TEST(VoicechatConvert, RoundTrip)
{
	const float values[] = { 0.0f, 1.0f, -1.0f, 0.5f, -123456.75f, 3.1415927f };
	for (float v : values)
	{
		unsigned char bytes[4];
		ComputeFloatToBytes(v, bytes);
		EXPECT_FLOAT_EQ(ComputeBytesToFloat(bytes), v);
	}
}

// [rc4l] The byte layout is little-endian: 1.0f is 0x3F800000 -> {00,00,80,3F}.
TEST(VoicechatConvert, LittleEndianLayout)
{
	unsigned char bytes[4];
	ComputeFloatToBytes(1.0f, bytes);
	EXPECT_EQ(bytes[0], 0x00);
	EXPECT_EQ(bytes[1], 0x00);
	EXPECT_EQ(bytes[2], 0x80);
	EXPECT_EQ(bytes[3], 0x3F);
	// And decoding those exact bytes gives 1.0f back.
	const unsigned char one[4] = { 0x00, 0x00, 0x80, 0x3F };
	EXPECT_FLOAT_EQ(ComputeBytesToFloat(one), 1.0f);
}

// [rc4l] Null pointers are handled: decode returns 0, encode is a no-op.
TEST(VoicechatConvert, NullPointers)
{
	EXPECT_FLOAT_EQ(ComputeBytesToFloat(nullptr), 0.0f);
	// Must not crash / write anywhere.
	ComputeFloatToBytes(42.0f, nullptr);
	SUCCEED();
}
} // namespace
