// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/panel-menu/computation/panelmenu_compute.h"

using namespace zx;

namespace
{
// x/y are the DRAWN corner -- the caller has already applied any patch offsets. h of 0 means the
// item cannot report its height, and the extent falls back to linespacing for it.
MenuItemBox Box(int x, int y, int w, int h = 0)
{
	MenuItemBox b;
	b.x = x; b.y = y; b.w = w; b.h = h;
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

// The regression this unit exists for. Freedoom's M_DOOM is offset (13,-16) and the menu places it
// with `StaticPatch 94, 2`, so it paints at (81, 18) -- the caller passes THAT, and the extent must
// follow the pixels rather than the stated position. Measuring 94,2 put the panel's top sixteen
// virtual rows too high, which at CleanYfac 4 was enough to drive it off-screen and get clamped
// flush to the edge.
TEST(ListMenuExtent, FollowsTheDrawnCornerNotTheStatedPosition)
{
	const MenuItemBox stated[] = { Box(94, 2, 159), Box(97, 72, 60) };
	const MenuItemBox drawn[]  = { Box(81, 18, 159), Box(97, 72, 60) };

	const MenuExtent a = ComputeListMenuExtent(stated, 2, 0, 16);
	const MenuExtent b = ComputeListMenuExtent(drawn, 2, 0, 16);

	EXPECT_EQ(2, a.top);
	EXPECT_EQ(18, b.top);			// sixteen rows lower, where the logo actually is
	EXPECT_EQ(253, a.right);		// 94 + 159, ignoring the leftoffset
	EXPECT_EQ(240, b.right);		// 81 + 159, which is centred on the 320-wide page
	EXPECT_EQ(88, b.bottom);		// bottom still tracks the rows, not the logo
}

// The second half of the same lesson: pad below the GLYPHS, not below the line box. A descriptor's
// linespacing is the gap between rows and runs taller than the font, so falling back to it under the
// last row left the leftover leading as extra margin -- the panel had a visibly bigger gap beneath
// its last row than above its first even after the top was correct.
TEST(ListMenuExtent, BottomFollowsDrawnHeightWhenTheItemReportsOne)
{
	const MenuItemBox reported[] = { Box(97, 72, 60, 11) };
	EXPECT_EQ(83, ComputeListMenuExtent(reported, 1, 0, 16).bottom);		// 72 + 11, not 72 + 16

	const MenuItemBox silent[] = { Box(97, 72, 60) };
	EXPECT_EQ(88, ComputeListMenuExtent(silent, 1, 0, 16).bottom);		// falls back to linespacing
}

TEST(ListMenuExtent, LowestPaintedEdgeWinsRegardlessOfOrder)
{
	// A short row lower down must not be beaten by a tall row above it, and vice versa.
	const MenuItemBox items[] = { Box(10, 10, 20, 40), Box(10, 60, 20, 5) };
	EXPECT_EQ(65, ComputeListMenuExtent(items, 2, 0, 16).bottom);

	const MenuItemBox tallLast[] = { Box(10, 10, 20, 5), Box(10, 20, 20, 90) };
	EXPECT_EQ(110, ComputeListMenuExtent(tallLast, 2, 0, 16).bottom);
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

// [rc4l] The panel is sized from menu items, and menu items are mod-supplied: a list menu draws
// StaticPatch "M_DOOM", and MM8BDM replaces that lump with a full-width banner where Doom's is
// 159x37. Unbounded, the card grew past the screen and became an opaque sheet over everything.
TEST(PanelBounds, OversizedContentIsClampedToLeaveAMargin)
{
	// A width far beyond the screen, as a replaced logo produces.
	const PanelBounds b = ComputePanelBounds(1280, 800, 4000, -300, 2000);
	EXPECT_EQ(1280 - 2 * (1280 / 16), b.w);		// margin on both sides
	EXPECT_EQ(800 / 16, b.top);					// pulled down off the top edge
	EXPECT_EQ(800 - 800 / 16, b.bottom);		// pulled up off the bottom edge
	EXPECT_LT(b.top, b.bottom);
}

TEST(PanelBounds, ContentThatAlreadyFitsIsLeftAlone)
{
	const PanelBounds b = ComputePanelBounds(1280, 800, 600, 200, 500);
	EXPECT_EQ(600, b.w);
	EXPECT_EQ(200, b.top);
	EXPECT_EQ(500, b.bottom);
}

TEST(PanelBounds, MarginScalesWithResolution)
{
	// A fixed pixel margin would look generous at 4K and swallow the card at 640x480.
	EXPECT_EQ(640 - 2 * (640 / 16), ComputePanelBounds(640, 480, 9999, 0, 9999).w);
	EXPECT_EQ(3840 - 2 * (3840 / 16), ComputePanelBounds(3840, 2160, 99999, 0, 99999).w);
}

TEST(PanelBounds, DegenerateInputsGiveAnEmptyRect)
{
	for (int i = 0; i < 3; ++i)
	{
		const int w = (i == 0) ? 0 : (i == 1) ? -10 : 1280;
		const int h = (i == 2) ? -1 : 800;
		const PanelBounds b = ComputePanelBounds(w, h, 500, 10, 200);
		EXPECT_EQ(0, b.w);
		EXPECT_EQ(0, b.top);
		EXPECT_EQ(0, b.bottom);
	}
}

TEST(PanelBounds, NeverReturnsAnInvertedRect)
{
	// Content taller than the screen would invert top/bottom without the guard.
	const PanelBounds b = ComputePanelBounds(1280, 800, 600, 700, 100);
	EXPECT_LE(b.top, b.bottom);
	EXPECT_GE(b.w, 0);
}
