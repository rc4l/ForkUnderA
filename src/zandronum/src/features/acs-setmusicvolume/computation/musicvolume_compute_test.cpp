// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the factor a client applies from FuaSetMusicVolume.
#include "gtest/gtest.h"
#include "features/acs-setmusicvolume/computation/musicvolume_compute.h"
#include <limits>

namespace
{
// [Doruk-Aksoy] A factor in range passes as sent.
TEST(ComputeNetMusicVolume, InRangePasses)
{
	EXPECT_FLOAT_EQ(ComputeNetMusicVolume(0.f), 0.f);
	EXPECT_FLOAT_EQ(ComputeNetMusicVolume(0.5f), 0.5f);
	EXPECT_FLOAT_EQ(ComputeNetMusicVolume(2.f), 2.f);
}

// [Doruk-Aksoy] Past either end it holds to I_SetMusicVolume's limits.
TEST(ComputeNetMusicVolume, ClampsToZeroAndTwo)
{
	EXPECT_FLOAT_EQ(ComputeNetMusicVolume(-1.f), 0.f);
	EXPECT_FLOAT_EQ(ComputeNetMusicVolume(5.f), 2.f);
}

// [Doruk-Aksoy] Not a number, or infinite, plays the song as it is.
TEST(ComputeNetMusicVolume, NonFiniteIsOne)
{
	EXPECT_FLOAT_EQ(ComputeNetMusicVolume(std::numeric_limits<float>::quiet_NaN()), 1.f);
	EXPECT_FLOAT_EQ(ComputeNetMusicVolume(std::numeric_limits<float>::infinity()), 1.f);
	EXPECT_FLOAT_EQ(ComputeNetMusicVolume(-std::numeric_limits<float>::infinity()), 1.f);
}
}
