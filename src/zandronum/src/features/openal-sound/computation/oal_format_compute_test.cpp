// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Tests for the OpenAL format/sizing math. Covers every branch.
#include "gtest/gtest.h"
#include "features/openal-sound/computation/oal_format_compute.h"

namespace
{
// [rc4l] Each valid channel/type combo maps to its AL format and frame size.
TEST(ComputeSampleFormat, ValidCombos)
{
	ZxSampleFormat m8 = ComputeSampleFormat(ZX_CHANNELCONFIG_MONO, ZX_SAMPLETYPE_UINT8);
	EXPECT_EQ(m8.format, ZX_AL_FORMAT_MONO8);
	EXPECT_EQ(m8.sampleSize, 1);

	ZxSampleFormat m16 = ComputeSampleFormat(ZX_CHANNELCONFIG_MONO, ZX_SAMPLETYPE_INT16);
	EXPECT_EQ(m16.format, ZX_AL_FORMAT_MONO16);
	EXPECT_EQ(m16.sampleSize, 2);

	ZxSampleFormat s8 = ComputeSampleFormat(ZX_CHANNELCONFIG_STEREO, ZX_SAMPLETYPE_UINT8);
	EXPECT_EQ(s8.format, ZX_AL_FORMAT_STEREO8);
	EXPECT_EQ(s8.sampleSize, 2);

	ZxSampleFormat s16 = ComputeSampleFormat(ZX_CHANNELCONFIG_STEREO, ZX_SAMPLETYPE_INT16);
	EXPECT_EQ(s16.format, ZX_AL_FORMAT_STEREO16);
	EXPECT_EQ(s16.sampleSize, 4);
}

// [rc4l] Unknown sample types (for each layout) and unknown layouts yield ZX_AL_NONE.
TEST(ComputeSampleFormat, UnsupportedCombos)
{
	EXPECT_EQ(ComputeSampleFormat(ZX_CHANNELCONFIG_MONO, 99).format, ZX_AL_NONE);
	EXPECT_EQ(ComputeSampleFormat(ZX_CHANNELCONFIG_STEREO, 99).format, ZX_AL_NONE);
	EXPECT_EQ(ComputeSampleFormat(42, ZX_SAMPLETYPE_INT16).format, ZX_AL_NONE);
}

// [rc4l] Channel counts for the known layouts and an unknown one.
TEST(ComputeChannelCount, KnownAndUnknown)
{
	EXPECT_EQ(ComputeChannelCount(ZX_CHANNELCONFIG_MONO), 1);
	EXPECT_EQ(ComputeChannelCount(ZX_CHANNELCONFIG_STEREO), 2);
	EXPECT_EQ(ComputeChannelCount(7), 0);
}

// [rc4l] 8-bit streams pick 8-bit formats by channel count.
TEST(ComputeStreamFormat, Bits8)
{
	EXPECT_EQ(ComputeStreamFormat(ZX_STREAM_BITS8 | ZX_STREAM_MONO, false), ZX_AL_FORMAT_MONO8);
	EXPECT_EQ(ComputeStreamFormat(ZX_STREAM_BITS8, false), ZX_AL_FORMAT_STEREO8);
}

// [rc4l] Float streams need the extension; without it they're unsupported.
TEST(ComputeStreamFormat, Float)
{
	EXPECT_EQ(ComputeStreamFormat(ZX_STREAM_FLOAT | ZX_STREAM_MONO, true), ZX_AL_FORMAT_MONO_FLOAT32);
	EXPECT_EQ(ComputeStreamFormat(ZX_STREAM_FLOAT, true), ZX_AL_FORMAT_STEREO_FLOAT32);
	EXPECT_EQ(ComputeStreamFormat(ZX_STREAM_FLOAT | ZX_STREAM_MONO, false), ZX_AL_NONE);
}

// [rc4l] 32-bit integer PCM is unsupported; everything else defaults to 16-bit.
TEST(ComputeStreamFormat, Bits32AndDefault16)
{
	EXPECT_EQ(ComputeStreamFormat(ZX_STREAM_BITS32, true), ZX_AL_NONE);
	EXPECT_EQ(ComputeStreamFormat(ZX_STREAM_MONO, false), ZX_AL_FORMAT_MONO16);
	EXPECT_EQ(ComputeStreamFormat(0, false), ZX_AL_FORMAT_STEREO16);
}

// [rc4l] Frame size is bytes-per-sample times channel count across the flag combinations.
TEST(ComputeStreamFrameSize, AllWidths)
{
	EXPECT_EQ(ComputeStreamFrameSize(ZX_STREAM_BITS8 | ZX_STREAM_MONO), 1);
	EXPECT_EQ(ComputeStreamFrameSize(ZX_STREAM_BITS8), 2);            // [rc4l] 1 byte, stereo
	EXPECT_EQ(ComputeStreamFrameSize(ZX_STREAM_FLOAT | ZX_STREAM_MONO), 4);
	EXPECT_EQ(ComputeStreamFrameSize(ZX_STREAM_BITS32), 8);          // [rc4l] 4 bytes, stereo
	EXPECT_EQ(ComputeStreamFrameSize(ZX_STREAM_MONO), 2);            // [rc4l] 16-bit mono
	EXPECT_EQ(ComputeStreamFrameSize(0), 4);                        // [rc4l] 16-bit stereo
}

// [rc4l] Buffer bytes round up to the next whole frame; exact multiples are unchanged.
TEST(ComputeAlignedBufferBytes, RoundsUp)
{
	EXPECT_EQ(ComputeAlignedBufferBytes(10, 4), 12);
	EXPECT_EQ(ComputeAlignedBufferBytes(12, 4), 12);
}
} // namespace
