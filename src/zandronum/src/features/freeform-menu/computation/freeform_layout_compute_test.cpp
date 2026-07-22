// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Tests for the freeform-menu layout math (gravity/anchor positioning, scroll
// range/centering, scroll clamping, hit-testing). Covers every branch.
#include "gtest/gtest.h"
#include "features/freeform-menu/computation/freeform_layout_compute.h"

namespace
{
// [rc4l] With scaleFac 1 and TOP|LEFT-ish gravity/anchor, position is just the logical pos.
TEST(ComputeAnchoredPosition, TopLeftIdentity)
{
	ZxScreenPos p = ComputeAnchoredPosition(10, 20, 4, 6, ZX_GRAV_TOP | ZX_GRAV_LEFT,
		ZX_GRAV_TOP | ZX_GRAV_LEFT, 0, 0, false, 1.0f, 640, 480);
	EXPECT_EQ(p.x, 10);
	EXPECT_EQ(p.y, 20);
}

// [rc4l] Right/bottom gravity offsets by the full screen size; center by half.
TEST(ComputeAnchoredPosition, GravityRightBottomAndCenter)
{
	ZxScreenPos rb = ComputeAnchoredPosition(0, 0, 0, 0, ZX_GRAV_RIGHT | ZX_GRAV_BOTTOM,
		ZX_GRAV_LEFT | ZX_GRAV_TOP, 0, 0, false, 1.0f, 640, 480);
	EXPECT_EQ(rb.x, 640);
	EXPECT_EQ(rb.y, 480);

	ZxScreenPos cc = ComputeAnchoredPosition(0, 0, 0, 0,
		ZX_GRAV_CENTER_HORIZONTAL | ZX_GRAV_CENTER_VERTICAL,
		ZX_GRAV_LEFT | ZX_GRAV_TOP, 0, 0, false, 1.0f, 640, 480);
	EXPECT_EQ(cc.x, 320);
	EXPECT_EQ(cc.y, 240);
}

// [rc4l] Right/bottom anchor subtracts the full scaled size; center anchor subtracts half.
TEST(ComputeAnchoredPosition, AnchorRightBottomAndCenter)
{
	ZxScreenPos rb = ComputeAnchoredPosition(100, 100, 40, 20, ZX_GRAV_LEFT | ZX_GRAV_TOP,
		ZX_GRAV_RIGHT | ZX_GRAV_BOTTOM, 0, 0, false, 1.0f, 640, 480);
	EXPECT_EQ(rb.x, 60);
	EXPECT_EQ(rb.y, 80);

	ZxScreenPos cc = ComputeAnchoredPosition(100, 100, 40, 20, ZX_GRAV_LEFT | ZX_GRAV_TOP,
		ZX_GRAV_CENTER_HORIZONTAL | ZX_GRAV_CENTER_VERTICAL, 0, 0, false, 1.0f, 640, 480);
	EXPECT_EQ(cc.x, 80);
	EXPECT_EQ(cc.y, 90);
}

// [rc4l] Padding is added only when requested, and is itself scaled.
TEST(ComputeAnchoredPosition, PaddingAndScale)
{
	ZxScreenPos without = ComputeAnchoredPosition(10, 10, 0, 0, ZX_GRAV_LEFT | ZX_GRAV_TOP,
		ZX_GRAV_LEFT | ZX_GRAV_TOP, 5, 7, false, 2.0f, 640, 480);
	EXPECT_EQ(without.x, 20);
	EXPECT_EQ(without.y, 20);

	ZxScreenPos with = ComputeAnchoredPosition(10, 10, 0, 0, ZX_GRAV_LEFT | ZX_GRAV_TOP,
		ZX_GRAV_LEFT | ZX_GRAV_TOP, 5, 7, true, 2.0f, 640, 480);
	EXPECT_EQ(with.x, 30); // [rc4l] 20 + 5*2
	EXPECT_EQ(with.y, 34); // [rc4l] 20 + 7*2
}

// [rc4l] Left/top anchor recenters a wider foreground leftward/upward; no anchor -> no shift.
TEST(ComputeForegroundAnchorOffset, LeftTop)
{
	ZxScreenOffset o = ComputeForegroundAnchorOffset(ZX_GRAV_LEFT | ZX_GRAV_TOP,
		40, 30, 20, 10, 1.0f);
	EXPECT_EQ(o.dx, -10); // [rc4l] -(40-20)/2
	EXPECT_EQ(o.dy, -10); // [rc4l] -(30-10)/2

	ZxScreenOffset none = ComputeForegroundAnchorOffset(
		ZX_GRAV_CENTER_HORIZONTAL | ZX_GRAV_CENTER_VERTICAL, 40, 30, 20, 10, 1.0f);
	EXPECT_EQ(none.dx, 0);
	EXPECT_EQ(none.dy, 0);
}

// [rc4l] Right/bottom anchor recenters a wider foreground the other way.
TEST(ComputeForegroundAnchorOffset, RightBottom)
{
	ZxScreenOffset o = ComputeForegroundAnchorOffset(ZX_GRAV_RIGHT | ZX_GRAV_BOTTOM,
		40, 30, 20, 10, 1.0f);
	EXPECT_EQ(o.dx, 10);
	EXPECT_EQ(o.dy, 10);
}

// [rc4l] When content fits, there's no scroll; centering halves the slack when enabled.
TEST(ComputeScrollBounds, FitsWithAndWithoutCentering)
{
	ZxScrollBounds centered = ComputeScrollBounds(100, 300, true);
	EXPECT_EQ(centered.lowestScroll, 0);
	EXPECT_EQ(centered.centeredOffset, 100); // [rc4l] (300-100)/2

	ZxScrollBounds notCentered = ComputeScrollBounds(100, 300, false);
	EXPECT_EQ(notCentered.lowestScroll, 0);
	EXPECT_EQ(notCentered.centeredOffset, 0);
}

// [rc4l] When content overflows, scroll range is the overflow and there's no centering.
TEST(ComputeScrollBounds, Overflows)
{
	ZxScrollBounds b = ComputeScrollBounds(500, 300, true);
	EXPECT_EQ(b.lowestScroll, 200);
	EXPECT_EQ(b.centeredOffset, 0);
}

// [rc4l] Scroll clamps to 0 below, to lowestScroll above, and passes through in range.
TEST(ComputeClampScroll, Clamps)
{
	EXPECT_EQ(ComputeClampScroll(-5, 100), 0);
	EXPECT_EQ(ComputeClampScroll(150, 100), 100);
	EXPECT_EQ(ComputeClampScroll(40, 100), 40);
}

// [rc4l] A point inside the half-open rectangle passes; each edge fails independently.
TEST(ComputePointInRect, InsideAndEachEdge)
{
	EXPECT_TRUE(ComputePointInRect(15, 25, 10, 20, 10, 10));
	EXPECT_FALSE(ComputePointInRect(9, 25, 10, 20, 10, 10));   // [rc4l] px < left
	EXPECT_FALSE(ComputePointInRect(20, 25, 10, 20, 10, 10));  // [rc4l] px >= left+width
	EXPECT_FALSE(ComputePointInRect(15, 19, 10, 20, 10, 10));  // [rc4l] py < top
	EXPECT_FALSE(ComputePointInRect(15, 30, 10, 20, 10, 10));  // [rc4l] py >= top+height
}
} // namespace
