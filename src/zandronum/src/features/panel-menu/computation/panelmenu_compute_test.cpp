// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/panel-menu/computation/panelmenu_compute.h"

using namespace zx;

namespace
{
MenuItemBox Box(int x, int y, int w, int inkTop = 0)
{
	MenuItemBox b;
	b.x = x; b.y = y; b.w = w; b.inkTop = inkTop;
	return b;
}
}

TEST(ListMenuExtent, EmptyInputIsNotValid)
{
	MenuExtent e = ComputeListMenuExtent(0, 0, 0, 16);
	EXPECT_FALSE(e.valid);

	MenuItemBox one = Box(10, 10, 20);
	EXPECT_FALSE(ComputeListMenuExtent(&one, 0, 0, 16).valid);
	EXPECT_FALSE(ComputeListMenuExtent(0, 3, 0, 16).valid);
}

TEST(ListMenuExtent, SpansAllItemsAndAddsLinespacing)
{
	const MenuItemBox items[] = { Box(100, 20, 40), Box(90, 60, 80), Box(110, 40, 20) };
	MenuExtent e = ComputeListMenuExtent(items, 3, 0, 16);

	ASSERT_TRUE(e.valid);
	EXPECT_EQ(90, e.left);
	EXPECT_EQ(170, e.right);		// 90 + 80
	EXPECT_EQ(20, e.top);
	EXPECT_EQ(76, e.bottom);		// lowest y 60, plus one linespacing
}

TEST(ListMenuExtent, NegativeCursorOffsetWidensLeftEdge)
{
	const MenuItemBox items[] = { Box(100, 10, 40) };

	EXPECT_EQ(100, ComputeListMenuExtent(items, 1, 0, 8).left);
	EXPECT_EQ(70, ComputeListMenuExtent(items, 1, -30, 8).left);
	// A positive offset means the cursor sits inside the row, so it must not move the edge.
	EXPECT_EQ(100, ComputeListMenuExtent(items, 1, 12, 8).left);
}

TEST(ListMenuExtent, SkipsOffPageAndZeroWidthItems)
{
	const MenuItemBox items[] = {
		Box(50, -1, 100),		// negative y: CleanNoMove, not part of the page
		Box(60, 30, 0),			// no reportable width
		Box(70, 40, 30),		// the only measurable one
	};
	MenuExtent e = ComputeListMenuExtent(items, 3, 0, 10);

	ASSERT_TRUE(e.valid);
	EXPECT_EQ(70, e.left);
	EXPECT_EQ(100, e.right);
	EXPECT_EQ(40, e.top);
	EXPECT_EQ(50, e.bottom);
}

TEST(ListMenuExtent, NothingMeasurableIsNotValid)
{
	const MenuItemBox items[] = { Box(50, -1, 100), Box(60, 30, 0) };
	EXPECT_FALSE(ComputeListMenuExtent(items, 2, 0, 10).valid);
}

// The regression this unit exists for: a logo authored with transparent slack above it used to push
// the panel's top edge up to the item's box, leaving a visibly bigger gap above the art than below
// the rows under it.
TEST(ListMenuExtent, InkTopMovesTopEdgeDownToTheArtwork)
{
	const MenuItemBox withSlack[] = { Box(94, 2, 200, 24), Box(97, 72, 60) };
	MenuExtent e = ComputeListMenuExtent(withSlack, 2, 0, 16);

	ASSERT_TRUE(e.valid);
	EXPECT_EQ(26, e.top);			// 2 + 24, not 2
	EXPECT_EQ(88, e.bottom);		// unchanged: bottom tracks the box
}

TEST(ListMenuExtent, InkTopNeverRaisesTheTopEdge)
{
	const MenuItemBox negative[] = { Box(94, 10, 200, -50) };
	EXPECT_EQ(10, ComputeListMenuExtent(negative, 1, 0, 16).top);

	const MenuItemBox zero[] = { Box(94, 10, 200, 0) };
	EXPECT_EQ(10, ComputeListMenuExtent(zero, 1, 0, 16).top);
}

TEST(ListMenuExtent, InkTopOnALaterItemStillWins)
{
	// The topmost BOX belongs to the logo, but after its slack the text above it is higher.
	const MenuItemBox items[] = { Box(94, 0, 200, 40), Box(97, 20, 60) };
	MenuExtent e = ComputeListMenuExtent(items, 2, 0, 16);

	ASSERT_TRUE(e.valid);
	EXPECT_EQ(20, e.top);
}

TEST(ListMenuExtent, SingleItemAndZeroLinespacing)
{
	const MenuItemBox items[] = { Box(10, 5, 15) };
	MenuExtent e = ComputeListMenuExtent(items, 1, 0, 0);

	ASSERT_TRUE(e.valid);
	EXPECT_EQ(10, e.left);
	EXPECT_EQ(25, e.right);
	EXPECT_EQ(5, e.top);
	EXPECT_EQ(5, e.bottom);
}
