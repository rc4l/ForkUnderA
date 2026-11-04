// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the relinks whose outcome is already known.
#include "gtest/gtest.h"
#include "features/relink-in-place/computation/relink_compute.h"

namespace
{
const ZxLinkedAt kLast{ 640, -128, 20, true, false, nullptr, nullptr };

ZxRelinkAsk Still() { return ZxRelinkAsk{ false, false, true, false, 640, -128, 20 }; }

// [Doruk-Aksoy] An actor still where its point last linked it, with its radius, relinks in place.
TEST(ComputeRelinkInPlace, StillWhereItWasLinked)
{
	EXPECT_TRUE(ComputeRelinkInPlace(Still(), kLast, false));
}

// [Doruk-Aksoy] Moving in x or y, the blockmap, being unlinked, polyobjects, another spot, radius or sector-list flag, or a link not by its point each relink in full.
TEST(ComputeRelinkInPlace, EverythingElseRelinksInFull)
{
	ZxRelinkAsk ask = Still(); ask.movesXY = true;
	EXPECT_FALSE(ComputeRelinkInPlace(ask, kLast, false));
	ask = Still(); ask.inBlockmap = true;
	EXPECT_FALSE(ComputeRelinkInPlace(ask, kLast, false));
	ask = Still(); ask.linked = false;
	EXPECT_FALSE(ComputeRelinkInPlace(ask, kLast, false));
	EXPECT_FALSE(ComputeRelinkInPlace(Still(), kLast, true));
	ask = Still(); ask.x += 1;
	EXPECT_FALSE(ComputeRelinkInPlace(ask, kLast, false));
	ask = Still(); ask.y -= 1;
	EXPECT_FALSE(ComputeRelinkInPlace(ask, kLast, false));
	ask = Still(); ask.radius = 0;
	EXPECT_FALSE(ComputeRelinkInPlace(ask, kLast, false));
	ask = Still(); ask.noSector = true;
	EXPECT_FALSE(ComputeRelinkInPlace(ask, kLast, false));
	ZxLinkedAt notByPoint = kLast; notByPoint.byPoint = false;
	EXPECT_FALSE(ComputeRelinkInPlace(Still(), notByPoint, false));
}

// [Doruk-Aksoy] Only a box with a radius can be straddled by a line.
TEST(ComputeSecNodesWalkLines, OnlyWithARadius)
{
	EXPECT_FALSE(ComputeSecNodesWalkLines(0));
	EXPECT_TRUE(ComputeSecNodesWalkLines(1));
	EXPECT_TRUE(ComputeSecNodesWalkLines(20 << 16));
}
}
