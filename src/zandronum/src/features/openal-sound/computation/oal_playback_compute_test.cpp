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
