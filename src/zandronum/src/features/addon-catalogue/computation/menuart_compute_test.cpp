// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/menuart_compute.h"

using zx::ArtRect;
using zx::LayoutMenuArt;
using zx::MenuArtFileName;

namespace
{

std::vector<std::pair<int, int> > Sizes(int w1, int h1, int w2 = 0, int h2 = 0)
{
	std::vector<std::pair<int, int> > out;
	out.push_back(std::make_pair(w1, h1));
	if (w2 > 0)
		out.push_back(std::make_pair(w2, h2));
	return out;
}

// The panel's own numbers, so the tests fail if the layout stops working at the size it is used at.
const int kSlotX = 328, kSlotY = 40, kSlotW = 252, kSlotH = 36, kGap = 8;

} // namespace

TEST(MenuArtFileName, AnEntryThatPlaysOneWayHasNoVariantInTheName)
{
	EXPECT_EQ("art.png", MenuArtFileName(""));
}

TEST(MenuArtFileName, AWayOfPlayingIsNamedAfterItself)
{
	// Named rather than numbered, so reordering the list does not silently repoint every picture.
	EXPECT_EQ("art.dm.png", MenuArtFileName("dm"));
	EXPECT_EQ("art.invasion.png", MenuArtFileName("invasion"));
}

TEST(LayoutMenuArt, NothingToDrawIsAnEmptyAnswer)
{
	// The caller's cue to draw the text header, so it must be distinguishable from a zero-size rect.
	EXPECT_TRUE(LayoutMenuArt(kSlotX, kSlotY, kSlotW, kSlotH, kGap,
		std::vector<std::pair<int, int> >()).empty());
}

TEST(LayoutMenuArt, OnePictureFillsTheHeightAndCentres)
{
	// 2:1, so at 36 tall it is 72 wide, which fits the slot easily.
	const std::vector<ArtRect> got = LayoutMenuArt(kSlotX, kSlotY, kSlotW, kSlotH, kGap,
		Sizes(128, 64));

	ASSERT_EQ(1u, got.size());
	EXPECT_EQ(36, got[0].h) << "the slot's height is the thing being filled";
	EXPECT_EQ(72, got[0].w);
	EXPECT_EQ(kSlotX + (kSlotW - 72) / 2, got[0].x);
	EXPECT_EQ(kSlotY, got[0].y);
}

TEST(LayoutMenuArt, KeepsTheAspectRatio)
{
	// Squashing a logo to fill a slot is worse than a smaller logo, so this is the whole point.
	const std::vector<ArtRect> got = LayoutMenuArt(0, 0, 400, 30, 0, Sizes(90, 30));

	ASSERT_EQ(1u, got.size());
	EXPECT_EQ(30, got[0].h);
	EXPECT_EQ(90, got[0].w);
}

TEST(LayoutMenuArt, AVeryWidePictureGivesUpHeightRatherThanWidth)
{
	// A logo six times as wide as it is tall. Filling the height would make it 216 wide and leave
	// nothing for the mix beside it, so it comes down in height instead and stays legible.
	const std::vector<ArtRect> got = LayoutMenuArt(kSlotX, kSlotY, kSlotW, kSlotH, kGap,
		Sizes(216, 36, 72, 36));

	ASSERT_EQ(2u, got.size());
	const int share = (kSlotW - kGap) / 2;

	EXPECT_EQ(share, got[0].w) << "held back to its share";
	EXPECT_LT(got[0].h, kSlotH) << "and it paid for that in height";
	EXPECT_EQ(kSlotH, got[1].h) << "its neighbour is unaffected";
}

TEST(LayoutMenuArt, TwoPicturesShareTheWidthWithTheGapBetweenThem)
{
	const std::vector<ArtRect> got = LayoutMenuArt(kSlotX, kSlotY, kSlotW, kSlotH, kGap,
		Sizes(72, 36, 72, 36));

	ASSERT_EQ(2u, got.size());
	EXPECT_EQ(got[0].x + got[0].w + kGap, got[1].x) << "exactly one gap, no overlap and no drift";
}

TEST(LayoutMenuArt, TwoPicturesStayInsideTheSlot)
{
	// The gap is taken out of the width BEFORE the shares are worked out. Doing it after is how the
	// second picture ends up hanging over the edge of the panel.
	const std::vector<ArtRect> got = LayoutMenuArt(kSlotX, kSlotY, kSlotW, kSlotH, kGap,
		Sizes(999, 36, 999, 36));

	ASSERT_EQ(2u, got.size());
	EXPECT_GE(got[0].x, kSlotX);
	EXPECT_LE(got[1].x + got[1].w, kSlotX + kSlotW);
}

TEST(LayoutMenuArt, ThePairIsCentredAsAGroup)
{
	// A narrow picture beside a wide one. The pair belongs in the middle of the slot; centring each
	// on its own share would leave the group visibly off to one side.
	const std::vector<ArtRect> got = LayoutMenuArt(kSlotX, kSlotY, kSlotW, kSlotH, kGap,
		Sizes(36, 36, 108, 36));

	ASSERT_EQ(2u, got.size());
	const int span = got[1].x + got[1].w - got[0].x;
	const int leftGap = got[0].x - kSlotX;
	const int rightGap = kSlotX + kSlotW - (got[1].x + got[1].w);

	EXPECT_EQ(36 + kGap + 108, span);
	EXPECT_LE(abs(leftGap - rightGap), 1) << "centred to within a rounded pixel";
}

TEST(LayoutMenuArt, PicturesOfDifferentHeightsAreEachCentredVertically)
{
	// Only happens when one was held back for width, and a pair sitting on different baselines
	// reads as a mistake.
	const std::vector<ArtRect> got = LayoutMenuArt(kSlotX, kSlotY, kSlotW, kSlotH, kGap,
		Sizes(216, 36, 36, 36));

	ASSERT_EQ(2u, got.size());
	for (size_t i = 0; i < got.size(); ++i)
	{
		const int above = got[i].y - kSlotY;
		const int below = kSlotY + kSlotH - (got[i].y + got[i].h);
		EXPECT_LE(abs(above - below), 1) << "picture " << i;
	}
}

TEST(LayoutMenuArt, ASlotWithNoRoomDrawsNothing)
{
	// Reachable while a panel is being sized, and a negative width must not become a huge one.
	EXPECT_TRUE(LayoutMenuArt(0, 0, 0, 36, kGap, Sizes(72, 36)).empty());
	EXPECT_TRUE(LayoutMenuArt(0, 0, 252, 0, kGap, Sizes(72, 36)).empty());
	EXPECT_TRUE(LayoutMenuArt(0, 0, -10, -10, kGap, Sizes(72, 36)).empty());
}

TEST(LayoutMenuArt, ASizeOfZeroIsNotDividedBy)
{
	// A texture that failed to load can report nothing. It must not take the panel down with it.
	const std::vector<ArtRect> got = LayoutMenuArt(kSlotX, kSlotY, kSlotW, kSlotH, kGap,
		Sizes(0, 0));

	ASSERT_EQ(1u, got.size());
	EXPECT_GE(got[0].w, 1);
	EXPECT_GE(got[0].h, 1);
}

TEST(LayoutMenuArt, NeverReturnsAnEmptyRectangle)
{
	// Swept, because a zero-width draw is invisible and looks exactly like art that failed to load.
	const int widths[] = { 1, 7, 36, 216, 999 };

	for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
	{
		const std::vector<ArtRect> got = LayoutMenuArt(kSlotX, kSlotY, kSlotW, kSlotH, kGap,
			Sizes(widths[i], 36, 72, 36));

		ASSERT_EQ(2u, got.size()) << "width=" << widths[i];
		for (size_t j = 0; j < got.size(); ++j)
		{
			EXPECT_GE(got[j].w, 1) << "width=" << widths[i] << " picture=" << j;
			EXPECT_GE(got[j].h, 1) << "width=" << widths[i] << " picture=" << j;
		}
	}
}
