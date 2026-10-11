// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/hwrender/computation/slotremap_compute.h"

using namespace zx::hwrender;

namespace
{
// [rc4l] WDI03's sewage, `texture BABWAS01 range BABWAS04 tics 3`, at texture indices 10..13.
AnimationDef Range() { AnimationDef a; a.basePic = 10; a.numFrames = 4; a.discrete = false; return a; }
// [rc4l] A discrete animation at base 20 showing 20, then 5.
AnimationDef Discrete() { AnimationDef a; a.basePic = 20; a.numFrames = 2; a.discrete = true; a.pics = { 20, 5 }; return a; }
}

TEST(SlotRemapCompute, EveryMemberOfARangeAnimatesButOnlyADiscreteBase)
{
	const std::vector<int> of = ComputeAnimationOfTexture(22, { Range(), Discrete() });
	ASSERT_EQ(22u, of.size());
	for (int id = 10; id <= 13; id++) EXPECT_EQ(0, of[id]) << id;
	EXPECT_EQ(1, of[20]);
	EXPECT_EQ(-1, of[5]);    // [rc4l] a discrete frame that is not the base shows itself
	EXPECT_EQ(-1, of[14]);
	EXPECT_EQ(-1, of[21]);
}

TEST(SlotRemapCompute, AnimationsReachingPastTheTextureListOrOverlappingKeepTheFirst)
{
	AnimationDef wide = Range(); wide.basePic = 20; wide.numFrames = 5;
	const std::vector<int> of = ComputeAnimationOfTexture(22, { Discrete(), wide });
	EXPECT_EQ(0, of[20]);
	EXPECT_EQ(1, of[21]);
	EXPECT_TRUE(ComputeAnimationOfTexture(0, { Range() }).empty());
	EXPECT_TRUE(ComputeAnimationOfTexture(-3, { Range() }).empty());
	AnimationDef below = Range(); below.basePic = -2; below.numFrames = 3;
	const std::vector<int> low = ComputeAnimationOfTexture(4, { below });
	EXPECT_EQ(0, low[0]);
	EXPECT_EQ(-1, low[1]);
}

TEST(SlotRemapCompute, AnimationFramesAreTheRangeOrTheListedPics)
{
	EXPECT_EQ((std::vector<int>{ 10, 11, 12, 13 }), ComputeAnimationFrames(Range()));
	EXPECT_EQ((std::vector<int>{ 20, 5 }), ComputeAnimationFrames(Discrete()));
}

TEST(SlotRemapCompute, ASlotFollowsWhatItsTextureShowsAndReportsAChange)
{
	std::vector<unsigned> remap;
	int frame = 2;
	// [rc4l] Slot 1 is the sewage's base, slots 2..5 its frames, slot 6 a still texture.
	auto shows = [&](unsigned i) { return i == 1 ? 2 + frame : -1; };
	EXPECT_TRUE(ComputeSlotRemap(7, shows, remap));
	EXPECT_EQ((std::vector<unsigned>{ 0, 4, 2, 3, 4, 5, 6 }), remap);
	EXPECT_FALSE(ComputeSlotRemap(7, shows, remap));
	frame = 3;
	EXPECT_TRUE(ComputeSlotRemap(7, shows, remap));
	EXPECT_EQ(5u, remap[1]);
}

TEST(SlotRemapCompute, SlotZeroAndAnswersOutsideTheTableKeepTheirOwnSlot)
{
	std::vector<unsigned> remap = { 9, 9 };
	auto shows = [](unsigned i) { return i == 1 ? 7 : 0; };
	EXPECT_TRUE(ComputeSlotRemap(3, shows, remap));
	EXPECT_EQ((std::vector<unsigned>{ 9, 1, 0 }), remap);   // [rc4l] slot 0 is never asked, a brightmap frame with none maps to 0
	EXPECT_FALSE(ComputeSlotRemap(0, shows, remap));
}

// [rc4l] WDI20's TV: a camera texture filled into the array before its first render came back white, and an unchanged table kept that fill for good.
TEST(SlotRemapCompute, AFillWithAWhiteLiveSlotIsTriedAgain)
{
	EXPECT_TRUE(ComputeKeepsMaterialFill(true, true, 0));
	EXPECT_FALSE(ComputeKeepsMaterialFill(true, true, 1));
	EXPECT_FALSE(ComputeKeepsMaterialFill(true, false, 0));
	EXPECT_FALSE(ComputeKeepsMaterialFill(false, true, 0));
}
