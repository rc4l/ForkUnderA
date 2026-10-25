// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the sound volume limits, with and without $fua_allow_amplify.
#include "gtest/gtest.h"
#include "features/sound-amplify/computation/sound_amplify_compute.h"

namespace
{
// [Doruk-Aksoy] Within range the call's volume and $volume simply multiply.
TEST(ComputeSoundVolume, MultipliesWithinRange)
{
	EXPECT_FLOAT_EQ(ComputeSoundVolume(0.5f, 0.5f, false), 0.25f);
	EXPECT_FLOAT_EQ(ComputeSoundVolume(0.5f, 1.5f, true), 0.75f);
}

// [Doruk-Aksoy] Without $fua_allow_amplify a sound stops at 1, as every sound did before it.
TEST(ComputeSoundVolume, ClampsToOneByDefault)
{
	EXPECT_FLOAT_EQ(ComputeSoundVolume(2.f, 1.f, false), 1.f);
	EXPECT_FLOAT_EQ(ComputeSoundVolume(1.f, 1.5f, false), 1.f);
}

// [Doruk-Aksoy] With it a sound goes past 1 up to the amplified limit and no further.
TEST(ComputeSoundVolume, AmplifiesUpToTheLimit)
{
	EXPECT_FLOAT_EQ(ComputeSoundVolume(1.5f, 1.f, true), 1.5f);
	EXPECT_FLOAT_EQ(ComputeSoundVolume(2.f, 1.5f, true), ZX_MAX_AMPLIFIED_VOLUME);
}

// [Doruk-Aksoy] A negative volume plays nothing rather than reaching OpenAL as an invalid gain.
TEST(ComputeSoundVolume, NegativeIsSilent)
{
	EXPECT_FLOAT_EQ(ComputeSoundVolume(-1.f, 1.f, false), 0.f);
	EXPECT_FLOAT_EQ(ComputeSoundVolume(1.f, -0.5f, true), 0.f);
}

// [Doruk-Aksoy] The limit is the most a server sends a client for a call's volume.
TEST(ComputeSoundVolume, LimitMatchesTheWire)
{
	EXPECT_FLOAT_EQ(ZX_MAX_AMPLIFIED_VOLUME, 2.f);
	EXPECT_EQ(ComputeSoundVolumeToWire(ZX_MAX_AMPLIFIED_VOLUME), 254);
}

// [Doruk-Aksoy] The bytes servers have always sent: the volume times 127, truncated, and held to 0..2.
TEST(ComputeSoundVolumeToWire, GoldenBytes)
{
	EXPECT_EQ(ComputeSoundVolumeToWire(0.f), 0);
	EXPECT_EQ(ComputeSoundVolumeToWire(0.5f), 63);
	EXPECT_EQ(ComputeSoundVolumeToWire(1.f), 127);
	EXPECT_EQ(ComputeSoundVolumeToWire(1.5f), 190);
	EXPECT_EQ(ComputeSoundVolumeToWire(2.f), 254);
	EXPECT_EQ(ComputeSoundVolumeToWire(3.f), 254);
	EXPECT_EQ(ComputeSoundVolumeToWire(-1.f), 0);
}

// [Doruk-Aksoy] A byte past 127 stays past 1, which a client used to cap at 1 before the sound's own limit was asked.
TEST(ComputeSoundVolumeFromWire, KeepsAmplifiedBytes)
{
	EXPECT_FLOAT_EQ(ComputeSoundVolumeFromWire(0), 0.f);
	EXPECT_FLOAT_EQ(ComputeSoundVolumeFromWire(127), 1.f);
	EXPECT_FLOAT_EQ(ComputeSoundVolumeFromWire(254), 2.f);
	EXPECT_FLOAT_EQ(ComputeSoundVolume(ComputeSoundVolumeFromWire(254), 1.f, false), 1.f);
	EXPECT_FLOAT_EQ(ComputeSoundVolume(ComputeSoundVolumeFromWire(255), 1.f, true), ZX_MAX_AMPLIFIED_VOLUME);
}

// [Doruk-Aksoy] Every volume a server can send arrives within one step of what it was.
TEST(ComputeSoundVolumeFromWire, RoundTripsWithinOneStep)
{
	for (int i = 0; i <= 200; ++i)
	{
		const float volume = i / 100.f;
		EXPECT_NEAR(ComputeSoundVolumeFromWire(ComputeSoundVolumeToWire(volume)), volume, 1.f / 127.f) << volume;
	}
}
}
