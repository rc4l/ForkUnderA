// [rc4l] Coverage-guided round-trip harnesses for the voice-chat float<->bytes serialization.
//
// This pair crosses the network in both directions, so the property that matters is that the
// two are exact inverses -- including for the bit patterns a float comparison cannot check
// (NaN never equals itself), which is why both directions compare raw bits rather than values.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "computation/voicechat_convert_compute.h"

namespace
{

uint32_t BitsOf(float f)
{
	uint32_t bits = 0;
	std::memcpy(&bits, &f, sizeof(bits));
	return bits;
}

// [rc4l] float -> bytes -> float must preserve the exact bit pattern, NaN payloads included.
void FloatSurvivesByteRoundtrip(float value)
{
	unsigned char bytes[4] = {0, 0, 0, 0};
	ComputeFloatToBytes(value, bytes);
	ASSERT_EQ(BitsOf(ComputeBytesToFloat(bytes)), BitsOf(value));
}
FUZZ_TEST(VoicechatConvertFuzz, FloatSurvivesByteRoundtrip);

// [rc4l] And the other direction: any 4 bytes off the wire must re-serialize to themselves.
void BytesSurviveFloatRoundtrip(const std::array<unsigned char, 4> &in)
{
	const float decoded = ComputeBytesToFloat(in.data());
	unsigned char out[4] = {0, 0, 0, 0};
	ComputeFloatToBytes(decoded, out);
	ASSERT_EQ(std::memcmp(in.data(), out, 4), 0) << "byte round-trip was not the identity";
}
FUZZ_TEST(VoicechatConvertFuzz, BytesSurviveFloatRoundtrip);

// [rc4l] The documented null-pointer guards, stated as total properties.
void NullPointersAreSafe(float value)
{
	ASSERT_EQ(ComputeBytesToFloat(nullptr), 0.0f);
	ComputeFloatToBytes(value, nullptr); // must not crash
}
FUZZ_TEST(VoicechatConvertFuzz, NullPointersAreSafe);

} // namespace
