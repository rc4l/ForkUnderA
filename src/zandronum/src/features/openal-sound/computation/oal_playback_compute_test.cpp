// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Tests for the OpenAL playback math (distance rolloff and loop points). Covers
// every branch.
#include "gtest/gtest.h"
#include "features/openal-sound/computation/oal_playback_compute.h"
#include <cmath>

namespace
{
// [rc4l] Inside the min distance the sound plays at full volume.
TEST(ComputeRolloff, FullVolumeWithinMinDistance)
{
	EXPECT_FLOAT_EQ(ComputeRolloff(ZX_ROLLOFF_LINEAR, 10.f, 100.f, 5.f, nullptr, 0), 1.f);
}

// [rc4l] Logarithmic rolloff uses the factor and never reaches silence.
TEST(ComputeRolloff, Logarithmic)
{
	// [rc4l] minDist/(minDist + factor*(dist-minDist)) = 1/(1+1*(3-1)).
	EXPECT_FLOAT_EQ(ComputeRolloff(ZX_ROLLOFF_LOG, 1.f, 1.f, 3.f, nullptr, 0), 1.f / 3.f);
}

// [rc4l] Beyond max distance a non-log rolloff is silent.
TEST(ComputeRolloff, SilentBeyondMaxDistance)
{
	EXPECT_FLOAT_EQ(ComputeRolloff(ZX_ROLLOFF_LINEAR, 1.f, 10.f, 20.f, nullptr, 0), 0.f);
}

// [rc4l] Linear rolloff returns the raw distance fraction.
TEST(ComputeRolloff, Linear)
{
	EXPECT_FLOAT_EQ(ComputeRolloff(ZX_ROLLOFF_LINEAR, 1.f, 11.f, 6.f, nullptr, 0), 0.5f);
}

// [rc4l] Custom rolloff with a curve looks up the SNDCURVE table.
TEST(ComputeRolloff, CustomCurve)
{
	const unsigned char curve[4] = { 0, 50, 100, 127 };
	// [rc4l] volume 0.5 -> index int(4 * (1 - 0.5)) = 2 -> 100/127.
	EXPECT_FLOAT_EQ(ComputeRolloff(ZX_ROLLOFF_CUSTOM, 1.f, 11.f, 6.f, curve, 4), 100.f / 127.f);
}

// [rc4l] Doom rolloff (and custom with no curve) fall back to the logarithmic volume scale.
TEST(ComputeRolloff, DoomAndCustomWithoutCurve)
{
	float expected = (powf(10.f, 0.5f) - 1.f) / 9.f;
	EXPECT_FLOAT_EQ(ComputeRolloff(ZX_ROLLOFF_DOOM, 1.f, 11.f, 6.f, nullptr, 0), expected);
	EXPECT_FLOAT_EQ(ComputeRolloff(ZX_ROLLOFF_CUSTOM, 1.f, 11.f, 6.f, nullptr, 0), expected);
}

// [rc4l] Unassigned loop points are scaled from ms to samples at the sample rate.
TEST(ComputeLoopPoints, ScalesUnassigned)
{
	ZxLoopPoints p = ComputeLoopPoints(1000, 2000, false, false, 44100, 1000000);
	EXPECT_EQ(p.start, 44100u);
	EXPECT_EQ(p.end, 88200u);
}

// [rc4l] Assigned points already in samples are left as-is when within range.
TEST(ComputeLoopPoints, AssignedInRange)
{
	ZxLoopPoints p = ComputeLoopPoints(5, 10, true, true, 44100, 100);
	EXPECT_EQ(p.start, 5u);
	EXPECT_EQ(p.end, 10u);
}

// [rc4l] An unset (~0u) end is not scaled and gets clamped down to the sample count.
TEST(ComputeLoopPoints, UnsetEndClampsToSamples)
{
	ZxLoopPoints p = ComputeLoopPoints(5, ~0u, true, false, 44100, 100);
	EXPECT_EQ(p.start, 5u);
	EXPECT_EQ(p.end, 100u);
}

// [rc4l] A start past the sample count resets to 0.
TEST(ComputeLoopPoints, StartPastSamplesResets)
{
	ZxLoopPoints p = ComputeLoopPoints(200, 50, true, true, 44100, 100);
	EXPECT_EQ(p.start, 0u);
	EXPECT_EQ(p.end, 50u);
}

// [rc4l] Loop points are programmed only when non-trivial, ordered, and AL supports them.
TEST(ComputeShouldSetLoopPoints, AllConditions)
{
	EXPECT_TRUE(ComputeShouldSetLoopPoints(5, 10, true));
	EXPECT_TRUE(ComputeShouldSetLoopPoints(0, 10, true));   // [rc4l] end>0 alone qualifies
	EXPECT_FALSE(ComputeShouldSetLoopPoints(0, 0, true));   // [rc4l] both zero
	EXPECT_FALSE(ComputeShouldSetLoopPoints(10, 5, true));  // [rc4l] end <= start
	EXPECT_FALSE(ComputeShouldSetLoopPoints(5, 10, false)); // [rc4l] AL lacks SOFT loop points
}
} // namespace

// [rc4l] The device's counts cap the channels, nothing reported means the channels, and Emscripten's two INT_MAX counts do not overflow into a negative size.
TEST(ComputeSourceCount, TheDeviceCapsTheChannels)
{
	EXPECT_EQ(32, ComputeSourceCount(255, 1, 32));
	EXPECT_EQ(16, ComputeSourceCount(15, 1, 32));
	EXPECT_EQ(32, ComputeSourceCount(0, 0, 32));
	EXPECT_EQ(32, ComputeSourceCount(2147483647, 2147483647, 32));
	EXPECT_EQ(32, ComputeSourceCount(-5, -1, 32));
}

// [rc4l] Doom 2's rocket explosion (14475 samples at 11025 Hz) ended at offset 14474 in a browser, short of its length, so the channel took it for evicted and the BFG's blast played twice.
TEST(ComputeSourcePosition, AStoppedSourceIsAtTheStart)
{
	EXPECT_EQ(0u, ComputeSourcePosition(true, 14474));
	EXPECT_EQ(0u, ComputeSourcePosition(true, 0));
}

TEST(ComputeSourcePosition, APlayingSourceIsWhereItPlays)
{
	EXPECT_EQ(7000u, ComputeSourcePosition(false, 7000));
	EXPECT_EQ(0u, ComputeSourcePosition(false, -1));
}

TEST(ComputeBufferSamples, BytesOverTheFrameSize)
{
	EXPECT_EQ(22050, ComputeBufferSamples(44100, 16, 1));
	EXPECT_EQ(11025, ComputeBufferSamples(44100, 16, 2));
	EXPECT_EQ(44100, ComputeBufferSamples(44100, 8, 1));
	EXPECT_EQ(0, ComputeBufferSamples(0, 16, 1));
	EXPECT_EQ(0, ComputeBufferSamples(44100, 0, 1));
	EXPECT_EQ(0, ComputeBufferSamples(44100, 16, 0));
}

// [rc4l] Loading a save restored every sound at the offset it had reached, which a browser's OpenAL refused past the end with dozens of "Invalid Value" errors (queue #71).
TEST(ComputeRestoredSampleOffset, InsideTheBufferWrappedOrOver)
{
	EXPECT_EQ(100, ComputeRestoredSampleOffset(100, 1000, false));
	EXPECT_EQ(0, ComputeRestoredSampleOffset(0, 1000, false));
	EXPECT_EQ(-1, ComputeRestoredSampleOffset(1000, 1000, false));
	EXPECT_EQ(-1, ComputeRestoredSampleOffset(5000, 1000, false));
	EXPECT_EQ(0, ComputeRestoredSampleOffset(1000, 1000, true));
	EXPECT_EQ(250, ComputeRestoredSampleOffset(2250, 1000, true));
	EXPECT_EQ(0, ComputeRestoredSampleOffset(5000, 0, false));
}
