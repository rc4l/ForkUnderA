// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/scrollbar_compute.h"

using zx::ComputeFirstFromPointer;
using zx::ComputeThumbHeight;
using zx::ComputeThumbTop;

namespace
{
// The shape that actually broke: 27 servers, 14 visible, so the thumb is over half the track and the
// error from ignoring its height is enormous.
const int kTrack = 224;
const int kVisible = 14;
const int kTotal = 27;
const int kMinThumb = 8;

int Thumb() { return ComputeThumbHeight(kTrack, kVisible, kTotal, kMinThumb); }
int MaxFirst() { return kTotal - kVisible; }
} // namespace

// ---------------------------------------------------------------- thumb size

TEST(ThumbHeight, IsProportionalToTheFractionVisible)
{
	EXPECT_EQ(112, ComputeThumbHeight(224, 10, 20, 4));
	EXPECT_EQ(56, ComputeThumbHeight(224, 5, 20, 4));
}

TEST(ThumbHeight, FillsTheTrackWhenEverythingFits)
{
	// A full thumb is how "there is nothing below" reads.
	EXPECT_EQ(224, ComputeThumbHeight(224, 14, 14, 4));
	EXPECT_EQ(224, ComputeThumbHeight(224, 20, 14, 4));
}

TEST(ThumbHeight, NeverShrinksBelowTheMinimum)
{
	// On a list of thousands the proportional thumb rounds to nothing, and a bar with no visible
	// thumb looks broken rather than full.
	EXPECT_EQ(8, ComputeThumbHeight(224, 14, 100000, 8));
}

TEST(ThumbHeight, SurvivesNonsensicalInputs)
{
	EXPECT_EQ(0, ComputeThumbHeight(0, 14, 27, 8));
	EXPECT_EQ(0, ComputeThumbHeight(-5, 14, 27, 8));
	EXPECT_EQ(224, ComputeThumbHeight(224, 0, 27, 8));
	EXPECT_EQ(224, ComputeThumbHeight(224, 14, 0, 8));
}

// ---------------------------------------------------------------- thumb position

TEST(ThumbTop, SitsAtTheTopForTheFirstRow)
{
	EXPECT_EQ(0, ComputeThumbTop(kTrack, Thumb(), 0, MaxFirst()));
}

TEST(ThumbTop, ReachesTheBottomOfTheTrackAtTheLastRow)
{
	// THE BUG, as geometry: the thumb travels over (track - thumb), not over the track. Anything
	// dividing by the track height puts the last row one whole thumb short of the end.
	const int thumb = Thumb();
	EXPECT_EQ(kTrack - thumb, ComputeThumbTop(kTrack, thumb, MaxFirst(), MaxFirst()));
}

TEST(ThumbTop, ClampsRatherThanRunningOffTheEnd)
{
	const int thumb = Thumb();
	EXPECT_EQ(0, ComputeThumbTop(kTrack, thumb, -5, MaxFirst()));
	EXPECT_EQ(kTrack - thumb, ComputeThumbTop(kTrack, thumb, 9999, MaxFirst()));
}

TEST(ThumbTop, StaysAtZeroWhenThereIsNowhereToScroll)
{
	EXPECT_EQ(0, ComputeThumbTop(kTrack, kTrack, 0, 0));
}

// ---------------------------------------------------------------- clicking the track

TEST(FirstFromPointer, CentresTheThumbOnThePointer)
{
	// The other half of the bug: mapping the pointer to the thumb's TOP makes every click undershoot
	// by half a thumb. Clicking the middle of the track should land in the middle of the list.
	const int thumb = Thumb();
	const int middle = ComputeFirstFromPointer(kTrack / 2, kTrack, thumb, MaxFirst());
	EXPECT_NEAR(MaxFirst() / 2, middle, 1);
}

TEST(FirstFromPointer, ClickingTheVeryTopGivesTheFirstRow)
{
	EXPECT_EQ(0, ComputeFirstFromPointer(0, kTrack, Thumb(), MaxFirst()));
}

TEST(FirstFromPointer, ClickingTheVeryBottomReachesTheLastRow)
{
	// Truncating instead of rounding leaves the final row unreachable by dragging, which feels like
	// the bar refusing to go all the way down.
	EXPECT_EQ(MaxFirst(), ComputeFirstFromPointer(kTrack, kTrack, Thumb(), MaxFirst()));
}

TEST(FirstFromPointer, PinsRatherThanRunningAwayPastEitherEnd)
{
	const int thumb = Thumb();
	EXPECT_EQ(0, ComputeFirstFromPointer(-500, kTrack, thumb, MaxFirst()));
	EXPECT_EQ(MaxFirst(), ComputeFirstFromPointer(kTrack + 500, kTrack, thumb, MaxFirst()));
}

TEST(FirstFromPointer, StaysPutWhenThereIsNothingToScroll)
{
	EXPECT_EQ(0, ComputeFirstFromPointer(100, kTrack, kTrack, 0));
	EXPECT_EQ(0, ComputeFirstFromPointer(100, kTrack, kTrack, 13));
}

// ---------------------------------------------------------------- the two agreeing

TEST(Scrollbar, ClickingWhereTheThumbIsDrawnDoesNotMoveIt)
{
	// The property that was violated. Draw the thumb for a row, click its centre, and you must get
	// that same row back -- otherwise the bar jumps the instant you touch it, which is exactly what
	// "very buggy" looked like.
	const int thumb = Thumb();
	const int maxFirst = MaxFirst();

	for (int first = 0; first <= maxFirst; ++first)
	{
		const int top = ComputeThumbTop(kTrack, thumb, first, maxFirst);
		const int centre = top + ( thumb / 2 );
		EXPECT_EQ(first, ComputeFirstFromPointer(centre, kTrack, thumb, maxFirst))
			<< "clicking the thumb drawn for row " << first << " moved it";
	}
}

TEST(Scrollbar, RoundTripsAcrossManyListShapes)
{
	// Same property over a spread of list sizes, since the thumb height changes with them and the
	// off-by-one-thumb error only shows up at some of them.
	const int shapes[][2] = { {15, 14}, {20, 14}, {27, 14}, {60, 14}, {500, 14}, {5000, 14} };

	for (size_t s = 0; s < sizeof(shapes) / sizeof(shapes[0]); ++s)
	{
		const int total = shapes[s][0];
		const int visible = shapes[s][1];
		const int maxFirst = total - visible;
		const int thumb = ComputeThumbHeight(kTrack, visible, total, kMinThumb);

		for (int first = 0; first <= maxFirst; first += ( maxFirst / 7 ) + 1)
		{
			const int top = ComputeThumbTop(kTrack, thumb, first, maxFirst);
			const int centre = top + ( thumb / 2 );
			const int back = ComputeFirstFromPointer(centre, kTrack, thumb, maxFirst);

			// [rc4l] Slack is ONE PIXEL's worth of rows, not one row -- which is the honest bound and
			// not the one I first wrote. A 224-pixel track cannot address 5000 rows: each pixel is
			// worth about 22 of them, so a round trip through pixel space is exact only while the
			// list is shorter than the track. Asserting one row there would be demanding precision
			// the representation does not have, and the test failing at 5000 was the test being
			// wrong rather than the arithmetic.
			const int travel = kTrack - thumb;
			const int rowsPerPixel = ( travel > 0 ) ? (( maxFirst / travel ) + 1 ) : maxFirst;

			EXPECT_NEAR(first, back, rowsPerPixel)
				<< "total " << total << ", row " << first << ", " << rowsPerPixel << " rows per pixel";
		}
	}
}

TEST( ThumbHeight, NeverGrowsPastTheTrackItRunsIn )
{
	// The minimum wins over the proportional size, so a caller asking for a minimum taller than the
	// track would otherwise get a thumb hanging off the end of it. The track is the harder limit: a
	// thumb longer than its own track cannot indicate a position.
	EXPECT_EQ( 20, ComputeThumbHeight( 20, 1, 100, 50 ));
	EXPECT_EQ( 20, ComputeThumbHeight( 20, 1, 100, 20 ));
}
