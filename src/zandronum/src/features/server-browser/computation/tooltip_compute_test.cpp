// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/tooltip_compute.h"

using zx::ComputeTooltipPlacement;
using zx::TooltipBox;
using zx::TooltipLines;
using zx::TooltipRectContains;
using std::string;
using std::vector;

namespace
{
const int kScreenW = 640;
const int kScreenH = 400;
const int kOffset = 12;
const int kMargin = 4;

TooltipBox Place( int px, int py, int w, int h )
{
	return ComputeTooltipPlacement( px, py, w, h, kScreenW, kScreenH, kOffset, kMargin );
}
} // namespace

// ---------------------------------------------------------------- hit testing

TEST( TooltipRect, ContainsItsOwnInterior )
{
	EXPECT_TRUE( TooltipRectContains( 10, 20, 30, 40, 10, 20 ));		// top-left corner is inside
	EXPECT_TRUE( TooltipRectContains( 10, 20, 30, 40, 25, 40 ));
	EXPECT_TRUE( TooltipRectContains( 10, 20, 30, 40, 39, 59 ));		// last pixel inside
}

TEST( TooltipRect, IsHalfOpenSoNeighboursDoNotOverlap )
{
	// Two rectangles that share an edge must not both claim the pixel on it, or which tooltip you get
	// depends on which was registered first.
	EXPECT_FALSE( TooltipRectContains( 10, 20, 30, 40, 40, 30 ));		// x + w
	EXPECT_FALSE( TooltipRectContains( 10, 20, 30, 40, 20, 60 ));		// y + h
}

TEST( TooltipRect, ExcludesEverythingOutside )
{
	EXPECT_FALSE( TooltipRectContains( 10, 20, 30, 40, 9, 30 ));
	EXPECT_FALSE( TooltipRectContains( 10, 20, 30, 40, 30, 19 ));
	EXPECT_FALSE( TooltipRectContains( 10, 20, 30, 40, -5, -5 ));
}

TEST( TooltipRect, AnEmptyRectangleContainsNothing )
{
	// A control that collapsed to nothing must not still be hoverable.
	EXPECT_FALSE( TooltipRectContains( 10, 20, 0, 40, 10, 30 ));
	EXPECT_FALSE( TooltipRectContains( 10, 20, 30, 0, 20, 20 ));
	EXPECT_FALSE( TooltipRectContains( 10, 20, -5, -5, 10, 20 ));
}

// ---------------------------------------------------------------- content

TEST( TooltipLines, SplitsOnNewlines )
{
	const vector<string> lines = TooltipLines( "brutalv21.wad\nmd5 abc123\n402 MB" );

	ASSERT_EQ( 3u, lines.size( ));
	EXPECT_EQ( "brutalv21.wad", lines[0] );
	EXPECT_EQ( "md5 abc123", lines[1] );
	EXPECT_EQ( "402 MB", lines[2] );
}

TEST( TooltipLines, ASingleLineIsOneLine )
{
	const vector<string> lines = TooltipLines( "Join this game" );
	ASSERT_EQ( 1u, lines.size( ));
	EXPECT_EQ( "Join this game", lines[0] );
}

TEST( TooltipLines, NothingInMeansNothingOut )
{
	// Not one empty line -- that would draw as an empty box hanging off the cursor.
	EXPECT_TRUE( TooltipLines( "" ).empty( ));
}

TEST( TooltipLines, KeepsDeliberateBlankLines )
{
	// A blank line between paragraphs is content, not noise.
	const vector<string> lines = TooltipLines( "a\n\nb" );
	ASSERT_EQ( 3u, lines.size( ));
	EXPECT_EQ( "", lines[1] );
}

TEST( TooltipLines, ATrailingNewlineGivesATrailingEmptyLine )
{
	const vector<string> lines = TooltipLines( "a\n" );
	ASSERT_EQ( 2u, lines.size( ));
	EXPECT_EQ( "a", lines[0] );
	EXPECT_EQ( "", lines[1] );
}

// ---------------------------------------------------------------- placement

TEST( TooltipPlacement, SitsDownAndRightOfThePointer )
{
	const TooltipBox box = Place( 100, 100, 80, 30 );
	EXPECT_EQ( 112, box.x );
	EXPECT_EQ( 112, box.y );
	EXPECT_EQ( 80, box.w );
	EXPECT_EQ( 30, box.h );
}

TEST( TooltipPlacement, FlipsToTheLeftRatherThanSlidingUnderTheCursor )
{
	// Near the right edge. Sliding left would put the box under the pointer; flipping to the other
	// side of it keeps the same clearance it had.
	const TooltipBox box = Place( 600, 100, 80, 30 );
	EXPECT_EQ( 600 - 12 - 80, box.x );
	EXPECT_LT( box.x + box.w, 600 );			// entirely left of the pointer
}

TEST( TooltipPlacement, FlipsUpwardsAtTheBottom )
{
	const TooltipBox box = Place( 100, 380, 80, 30 );
	EXPECT_EQ( 380 - 12 - 30, box.y );
	EXPECT_LT( box.y + box.h, 380 );
}

TEST( TooltipPlacement, FlipsBothWaysInACorner )
{
	const TooltipBox box = Place( 630, 395, 80, 30 );
	EXPECT_LT( box.x + box.w, 630 );
	EXPECT_LT( box.y + box.h, 395 );
}

TEST( TooltipPlacement, StaysOnScreenWhereverThePointerIs )
{
	// The invariant, swept: for every pointer position and a range of box sizes that fit, the box is
	// inside the margins. A tooltip half off the screen is a tooltip you cannot read.
	const int sizes[][2] = { { 40, 20 }, { 120, 30 }, { 300, 90 }, { 600, 380 } };

	for ( int px = -20; px <= kScreenW + 20; px += 17 )
		for ( int py = -20; py <= kScreenH + 20; py += 13 )
			for ( int s = 0; s < 4; ++s )
			{
				const TooltipBox box = Place( px, py, sizes[s][0], sizes[s][1] );

				EXPECT_GE( box.x, kMargin ) << px << "," << py << " size " << s;
				EXPECT_GE( box.y, kMargin ) << px << "," << py << " size " << s;
				EXPECT_LE( box.x + box.w, kScreenW - kMargin ) << px << "," << py << " size " << s;
				EXPECT_LE( box.y + box.h, kScreenH - kMargin ) << px << "," << py << " size " << s;
			}
}

TEST( TooltipPlacement, NeverShrinksTheBoxItWasGiven )
{
	// The caller decided what the content is. Cropping it here would hide the part that mattered --
	// the hash, or the size, or whatever was on the last line.
	for ( int w = 10; w <= 900; w += 97 )
		for ( int h = 10; h <= 600; h += 61 )
		{
			const TooltipBox box = Place( 320, 200, w, h );
			EXPECT_EQ( w, box.w );
			EXPECT_EQ( h, box.h );
		}
}

TEST( TooltipPlacement, ABoxTooBigForTheScreenStartsAtTheMargin )
{
	// Neither side fits, so the top-left is what is kept: the start of the text is the part worth
	// having when something has to be lost.
	const TooltipBox box = Place( 320, 200, 900, 600 );
	EXPECT_EQ( kMargin, box.x );
	EXPECT_EQ( kMargin, box.y );
}

TEST( TooltipPlacement, NeverSitsUnderThePointerWhenItHasRoomNotTo )
{
	// The reason for the offset in the first place: text under the cursor is text you are covering
	// with your own hand.
	const int sizes[][2] = { { 40, 20 }, { 120, 30 } };

	for ( int px = 40; px <= kScreenW - 40; px += 23 )
		for ( int py = 40; py <= kScreenH - 40; py += 19 )
			for ( int s = 0; s < 2; ++s )
			{
				const TooltipBox box = Place( px, py, sizes[s][0], sizes[s][1] );

				EXPECT_FALSE( TooltipRectContains( box.x, box.y, box.w, box.h, px, py ))
					<< px << "," << py << " size " << s;
			}
}
