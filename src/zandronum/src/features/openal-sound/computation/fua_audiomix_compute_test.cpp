// [rc4l] Tests for the instant-replay audio pump's pure parts. See fua_audiomix_compute.h.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"
#include "features/openal-sound/computation/fua_audiomix_compute.h"

#include <limits>

namespace
{

TEST(FuaAudioFrames, BytesConvertToWholeStereoFrames)
{
	EXPECT_EQ(zx::FuaFramesForBytes(4), 1);      // one stereo S16 frame is 4 bytes
	EXPECT_EQ(zx::FuaFramesForBytes(4096), 1024);
}

TEST(FuaAudioFrames, PartialFramesAreNotReported)
{
	// A partial frame has no meaning downstream -- half a stereo sample is not something the
	// recorder or the device can use -- so it is dropped rather than rounded up.
	EXPECT_EQ(zx::FuaFramesForBytes(3), 0);
	EXPECT_EQ(zx::FuaFramesForBytes(5), 1);
	EXPECT_EQ(zx::FuaFramesForBytes(0), 0);
	EXPECT_EQ(zx::FuaFramesForBytes(-8), 0);
}

TEST(FuaAudioScratch, ClampsToWhatTheBufferHolds)
{
	// The buffer is interleaved stereo, so its frame capacity is half its float count.
	EXPECT_EQ(zx::FuaClampFramesToScratch(1024, zx::kFuaAudioScratch), 1024);
	EXPECT_EQ(zx::FuaClampFramesToScratch(9000, zx::kFuaAudioScratch), zx::kFuaAudioScratch / 2);
	EXPECT_EQ(zx::FuaClampFramesToScratch(4096, 8192), 4096);
	EXPECT_EQ(zx::FuaClampFramesToScratch(4097, 8192), 4096);
}

TEST(FuaAudioScratch, DegenerateInputsYieldNothing)
{
	EXPECT_EQ(zx::FuaClampFramesToScratch(0, zx::kFuaAudioScratch), 0);
	EXPECT_EQ(zx::FuaClampFramesToScratch(-1, zx::kFuaAudioScratch), 0);
	EXPECT_EQ(zx::FuaClampFramesToScratch(1024, 0), 0);
	EXPECT_EQ(zx::FuaClampFramesToScratch(1024, -4), 0);
}

TEST(FuaFloatToS16, ScalesFullRange)
{
	const float in[4] = { 0.0f, 1.0f, -1.0f, 0.5f };
	short out[4] = { 1, 1, 1, 1 };
	zx::FuaFloatToS16(in, out, 2);   // 2 frames == 4 interleaved samples
	EXPECT_EQ(out[0], 0);
	EXPECT_EQ(out[1], 32767);
	EXPECT_EQ(out[2], -32767);
	EXPECT_EQ(out[3], static_cast<short>(0.5f * 32767.0f));
}

TEST(FuaFloatToS16, ClampsRatherThanWrapping)
{
	// The case that matters: a loopback mix can exceed unity when loud sources coincide. Wrapping
	// would turn that into a click; clamping keeps it merely loud.
	const float in[4] = { 4.0f, -4.0f, 1.0001f, -1.0001f };
	short out[4] = {};
	zx::FuaFloatToS16(in, out, 2);
	EXPECT_EQ(out[0], 32767);
	EXPECT_EQ(out[1], -32767);
	EXPECT_EQ(out[2], 32767);
	EXPECT_EQ(out[3], -32767);
}

TEST(FuaFloatToS16, NullOrEmptyIsANoOp)
{
	const float in[2] = { 1.0f, 1.0f };
	short out[2] = { 7, 7 };

	zx::FuaFloatToS16(nullptr, out, 1);
	EXPECT_EQ(out[0], 7);

	zx::FuaFloatToS16(in, nullptr, 1);   // must not crash

	zx::FuaFloatToS16(in, out, 0);
	EXPECT_EQ(out[0], 7);
	zx::FuaFloatToS16(in, out, -3);
	EXPECT_EQ(out[0], 7);
}

TEST(FuaAudioContract, TheRateAndLayoutAreFixedInOnePlace)
{
	// The sinks must not disagree about this; pinned so a change is deliberate.
	EXPECT_EQ(zx::kFuaAudioRate, 44100);
	EXPECT_EQ(zx::kFuaAudioChannels, 2);
	EXPECT_EQ(zx::kFuaAudioFrames, 1024);
	EXPECT_EQ(zx::FuaFramesForBytes(zx::kFuaAudioFrames * zx::kFuaAudioChannels * 2),
	          zx::kFuaAudioFrames);
}

} // namespace
