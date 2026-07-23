// [rc4l] Tests for the pure OpenAL sound-menu decision logic (oal_menu_compute).
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/openal-sound/computation/oal_menu_compute.h"

// --- HRTF tri-state (snd_hrtf -> ALC attribute) ---

TEST(OalMenuCompute, HrtfAttribTristate)
{
	EXPECT_EQ(ComputeHrtfAttrib(0), ZX_HRTF_DISABLE);   // Off
	EXPECT_EQ(ComputeHrtfAttrib(1), ZX_HRTF_ENABLE);    // On
	EXPECT_EQ(ComputeHrtfAttrib(2), ZX_HRTF_ENABLE);    // any positive == On
	EXPECT_EQ(ComputeHrtfAttrib(-1), ZX_HRTF_DONTCARE); // Auto (AutoOffOn's -1)
	EXPECT_EQ(ComputeHrtfAttrib(-100), ZX_HRTF_DONTCARE);
}

// --- music mode -> AL stereo / direct-channels modes ---

TEST(OalMenuCompute, StereoModeSoft)
{
	EXPECT_EQ(ComputeStereoModeSoft(ZX_MUSICMODE_NORMAL), ZX_AL_NORMAL_SOFT);
	EXPECT_EQ(ComputeStereoModeSoft(ZX_MUSICMODE_DIRECTMIX), ZX_AL_NORMAL_SOFT);   // only SuperStereo is UHJ
	EXPECT_EQ(ComputeStereoModeSoft(ZX_MUSICMODE_SUPERSTEREO), ZX_AL_SUPER_STEREO_SOFT);
}

TEST(OalMenuCompute, DirectMixRemix)
{
	EXPECT_FALSE(ComputeDirectMixRemix(ZX_MUSICMODE_NORMAL));
	EXPECT_TRUE(ComputeDirectMixRemix(ZX_MUSICMODE_DIRECTMIX));
	EXPECT_FALSE(ComputeDirectMixRemix(ZX_MUSICMODE_SUPERSTEREO));
}

// --- resampler name -> index lookup ---

TEST(OalMenuCompute, ResamplerIndexDefaultKeepsDriverDefault)
{
	const std::vector<std::string> names{"Point", "Linear", "Cubic Spline"};
	// "Default"/empty never search -- they keep whatever the driver's default index is.
	EXPECT_EQ(ComputeResamplerIndex(names, 1, "Default"), 1);
	EXPECT_EQ(ComputeResamplerIndex(names, 2, ""), 2);
	EXPECT_EQ(ComputeResamplerIndex({}, 0, "Default"), 0); // empty list + default is fine
}

TEST(OalMenuCompute, ResamplerIndexNamedMatch)
{
	const std::vector<std::string> names{"Point", "Linear", "Cubic Spline"};
	EXPECT_EQ(ComputeResamplerIndex(names, 0, "Point"), 0);
	EXPECT_EQ(ComputeResamplerIndex(names, 0, "Linear"), 1);
	EXPECT_EQ(ComputeResamplerIndex(names, 0, "Cubic Spline"), 2); // names may contain spaces
}

TEST(OalMenuCompute, ResamplerIndexNamedButAbsentIsMinusOne)
{
	const std::vector<std::string> names{"Point", "Linear"};
	EXPECT_EQ(ComputeResamplerIndex(names, 1, "Nonexistent"), -1); // caller warns + uses default
	EXPECT_EQ(ComputeResamplerIndex({}, 0, "Point"), -1);          // absent because the list is empty
}

// --- OpenAL double-NUL-terminated name-list parsing ---

TEST(OalMenuCompute, ParseAlNameListNullAndEmpty)
{
	EXPECT_TRUE(ParseAlNameList(nullptr).empty());
	EXPECT_TRUE(ParseAlNameList("\0").empty()); // the immediate terminator: no entries
}

TEST(OalMenuCompute, ParseAlNameListMultiple)
{
	// Two devices then the list terminator, exactly as alcGetString returns them.
	const char list[] = "OpenAL Soft\0Built-in Audio\0";
	const auto got = ParseAlNameList(list);
	ASSERT_EQ(got.size(), 2u);
	EXPECT_EQ(got[0], "OpenAL Soft");
	EXPECT_EQ(got[1], "Built-in Audio");
}

TEST(OalMenuCompute, ParseAlNameListSingle)
{
	const char list[] = "Default Device\0";
	const auto got = ParseAlNameList(list);
	ASSERT_EQ(got.size(), 1u);
	EXPECT_EQ(got[0], "Default Device");
}
