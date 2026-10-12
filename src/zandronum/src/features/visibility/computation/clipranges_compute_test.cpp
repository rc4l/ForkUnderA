// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/clipranges_compute.h"

#include <gtest/gtest.h>

using namespace zx::visibility;

namespace {

void Expect(const ClipRanges &c, std::initializer_list<std::pair<unsigned, unsigned>> want)
{
	ASSERT_EQ(c.Count(), (int)want.size());
	int i = 0;
	for (auto &w : want)
	{
		EXPECT_EQ(c.Start(i), w.first) << "range " << i;
		EXPECT_EQ(c.End(i), w.second) << "range " << i;
		i++;
	}
}

} // namespace

TEST(ClipRanges, AnEmptyClipperSeesEverything)
{
	ClipRanges c;
	EXPECT_TRUE(c.SafeCheckRange(10, 20));
	EXPECT_TRUE(c.SafeCheckRange(0xF0000000u, 0x10u));   // [rc4l] a range across the wrap
}

TEST(ClipRanges, BlockedRangesMergeAndHideWhatTheyCover)
{
	ClipRanges c;
	c.SafeAddClipRange(100, 200);
	c.SafeAddClipRange(300, 400);
	Expect(c, { { 100, 200 }, { 300, 400 } });
	EXPECT_FALSE(c.SafeCheckRange(120, 180));
	EXPECT_TRUE(c.SafeCheckRange(150, 250));   // [rc4l] partly open
	c.SafeAddClipRange(50, 60);                 // [rc4l] before the first
	c.SafeAddClipRange(150, 350);               // [rc4l] bridging two
	Expect(c, { { 50, 60 }, { 100, 400 } });
	c.SafeAddClipRange(10, 500);                // [rc4l] covering everything
	Expect(c, { { 10, 500 } });
	c.SafeAddClipRange(20, 30);                 // [rc4l] already covered
	Expect(c, { { 10, 500 } });
	c.SafeAddClipRange(500, 600);               // [rc4l] touching grows it
	c.SafeAddClipRange(5, 10);
	Expect(c, { { 5, 600 } });
	c.SafeAddClipRange(700, 800);
	c.SafeAddClipRange(650, 900);               // [rc4l] swallowing one it overlaps
	Expect(c, { { 5, 600 }, { 650, 900 } });
	c.SafeAddClipRange(620, 640);               // [rc4l] between two
	Expect(c, { { 5, 600 }, { 620, 640 }, { 650, 900 } });
	c.SafeAddClipRange(610, 660);               // [rc4l] its growth reaching the next, which outlasts it
	Expect(c, { { 5, 600 }, { 610, 900 } });
}

TEST(ClipRanges, ARangeAcrossTheWrapSplitsInTwo)
{
	ClipRanges c;
	c.SafeAddClipRange(0xF0000000u, 0x10u);
	Expect(c, { { 0, 0x10u }, { 0xF0000000u, 0xFFFFFFFFu } });
	EXPECT_FALSE(c.SafeCheckRange(0xF8000000u, 0x8u));
	EXPECT_FALSE(c.SafeCheckRange(0u, 0u));   // [rc4l] an empty range at zero, against a range starting there
	c.SafeRemoveClipRange(0xF8000000u, 0x8u);
	Expect(c, { { 0x8u, 0x10u }, { 0xF0000000u, 0xF8000000u } });
}

TEST(ClipRanges, RemovingTrimsSplitsAndDrops)
{
	ClipRanges c;
	c.SafeAddClipRange(100, 400);
	c.SafeRemoveClipRange(200, 300);   // [rc4l] out of the middle
	Expect(c, { { 100, 200 }, { 300, 400 } });
	c.SafeRemoveClipRange(50, 150);    // [rc4l] the front of one
	Expect(c, { { 150, 200 }, { 300, 400 } });
	c.SafeRemoveClipRange(350, 450);   // [rc4l] the back of one
	Expect(c, { { 150, 200 }, { 300, 350 } });
	c.SafeRemoveClipRange(100, 250);   // [rc4l] all of one
	Expect(c, { { 300, 350 } });
	c.Clear();
	c.SafeRemoveClipRange(1, 2);       // [rc4l] nothing to remove
	Expect(c, {});
}

TEST(ClipRanges, ASilhouetteSurvivesRemoval)
{
	ClipRanges c;
	c.SafeAddClipRange(100, 200);
	c.SafeAddClipRange(300, 400);
	c.SetSilhouette();
	c.SetSilhouette();   // [rc4l] a second call keeps the first, as GL's does
	c.SafeAddClipRange(500, 600);
	c.SafeRemoveClipRange(0, 1000);
	Expect(c, { { 100, 200 }, { 300, 400 } });
	c.SafeRemoveClipRange(150, 180);   // [rc4l] wholly inside the silhouette: nothing happens
	Expect(c, { { 100, 200 }, { 300, 400 } });
	c.SafeAddClipRange(200, 300);
	c.SafeRemoveClipRange(150, 350);   // [rc4l] starting inside it, only the gap between goes
	Expect(c, { { 100, 200 }, { 300, 400 } });
	c.SafeAddClipRange(450, 460);
	c.SafeRemoveClipRange(390, 405);   // [rc4l] ending inside the silhouette leaves the rest alone
	Expect(c, { { 100, 200 }, { 300, 400 }, { 450, 460 } });
	c.Clear();
	EXPECT_EQ(c.Count(), 0);
}

TEST(ClipRanges, PseudoAnglesOrderTheCompass)
{
	EXPECT_EQ(ComputePseudoAngle(0, 0), 0u);
	EXPECT_EQ(ComputePseudoAngle(1, 0), 0u);              // [rc4l] east
	EXPECT_EQ(ComputePseudoAngle(0, 1), 0x40000000u);     // [rc4l] north: 1 of 4
	EXPECT_EQ(ComputePseudoAngle(-1, 0), 0x80000000u);    // [rc4l] west: 2
	EXPECT_EQ(ComputePseudoAngle(0, -1), 0xC0000000u);    // [rc4l] south: 3
	EXPECT_EQ(ComputePseudoAngle(1, -1), 0xE0000000u);    // [rc4l] south-east wraps from below zero
	EXPECT_LT(ComputePseudoAngle(10, 1), ComputePseudoAngle(10, 2));
}

TEST(ClipRanges, TrueAnglesMapOntoTheSameCompass)
{
	EXPECT_EQ(ComputeAngleToPseudo(0), 0u);
	EXPECT_EQ(ComputeAngleToPseudo(0x40000000u), 0x40000000u);   // [rc4l] 90 degrees
	EXPECT_EQ(ComputeAngleToPseudo(0x80000000u), 0x80000000u);
	EXPECT_LT(ComputeAngleToPseudo(0x10000000u), ComputeAngleToPseudo(0x20000000u));
}

TEST(ClipRanges, TheFrustumBlocksEverythingBehindTheView)
{
	ClipRanges c;
	ComputeSeedFrustum(c, 0x40000000u, 0x15555555u);   // [rc4l] looking north, 30 degrees either side
	EXPECT_TRUE(c.SafeCheckRange(ComputeAngleToPseudo(0x38000000u), ComputeAngleToPseudo(0x48000000u)));
	EXPECT_FALSE(c.SafeCheckRange(ComputeAngleToPseudo(0xB8000000u), ComputeAngleToPseudo(0xC8000000u)));   // [rc4l] south
	ClipRanges all;
	ComputeSeedFrustum(all, 0x40000000u, 0xFFFFFFFFu);   // [rc4l] pitched far enough to see all round
	EXPECT_TRUE(all.SafeCheckRange(ComputeAngleToPseudo(0xB8000000u), ComputeAngleToPseudo(0xC8000000u)));
}

TEST(ClipRanges, ABoxIsVisibleUnlessItsSpanIsBlocked)
{
	ClipRanges c;
	const long long box[4] = { 200, 100, 100, 200 };   // [rc4l] top, bottom, left, right
	EXPECT_TRUE(ComputeBoxVisible(c, 150, 150, box));   // [rc4l] standing inside it
	EXPECT_TRUE(ComputeBoxVisible(c, 0, 150, box));
	// [rc4l] Seen from the west the box spans about 14 degrees either side of east; block east and it is gone.
	c.SafeAddClipRange(0xE0000000u, 0x20000000u);
	EXPECT_FALSE(ComputeBoxVisible(c, 0, 150, box));
	EXPECT_TRUE(ComputeBoxVisible(c, 150, 400, box));   // [rc4l] from the north it lies south, which is open
	EXPECT_TRUE(ComputeBoxVisible(c, 400, 0, box));     // [rc4l] from the south-east corner
}
