// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/global-header/computation/globalheader_compute.h"

using zx::DefaultHeaderMetrics;
using zx::HeaderBarContains;
using zx::HeaderMetrics;
using zx::HeaderRect;
using zx::HeaderTabAtPoint;
using zx::HeaderTabRect;
using zx::MenuClearanceY;
using zx::kHeaderTabCount;
using zx::StepHeaderTab;
using zx::CursorAtTopRow;

namespace
{

// Two labels of very different widths, which is the case the layout exists to handle.
const int kWidths[] = { 40, 62 };
const int kCount = 2;

} // namespace

// ---------------------------------------------------------------- metrics

TEST( GlobalHeader, TheBarIsTallerThanThePillsItHolds )
{
	// The pills sit ON a surface. A bar exactly as tall as its contents is not a bar, it is a row of
	// buttons stuck to the top edge, and the header is meant to read as chrome.
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_GT( m.barH, m.tabH );
	EXPECT_GE( m.tabTop, 1 );
	EXPECT_LE( m.tabTop + m.tabH, m.barH );
}

TEST( GlobalHeader, EveryMenuIsPushedClearOfTheBarRatherThanUpAgainstIt )
{
	// The bug this is here to stop: the clearance was the bar height exactly, so the OPTIONS title
	// came to rest ON the bar and the two read as one piece of furniture.
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_GT( m.menuGap, 0 );
	EXPECT_GT( MenuClearanceY( m ), ( m.barH + 1 ) / 2 );
}

TEST( GlobalHeader, TheClearanceAlwaysCoversTheBarWhateverTheBarBecomes )
{
	// Swept, because the whole point of deriving it is that the bar is going to change. Half of the
	// bar's own height in the menus' space is the floor; anything less is a bar drawn over a menu.
	for ( int barH = 0; barH <= 200; ++barH )
	{
		HeaderMetrics m = DefaultHeaderMetrics( );
		m.barH = barH;

		EXPECT_GE( MenuClearanceY( m ) * 2, barH ) << "barH " << barH;
	}
}

TEST( GlobalHeader, NoGapConfiguredStillClearsTheBar )
{
	// A caller that zeroes the gap wants no air, not an overlap.
	HeaderMetrics m = DefaultHeaderMetrics( );
	m.menuGap = 0;

	EXPECT_GE( MenuClearanceY( m ) * 2, m.barH );
}

// ----------------------------------------------------------------- layout

TEST( GlobalHeader, EachPillIsItsLabelPlusPaddingBothSides )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_EQ( kWidths[0] + 2 * m.labelPad, HeaderTabRect( m, kWidths, kCount, 0 ).w );
	EXPECT_EQ( kWidths[1] + 2 * m.labelPad, HeaderTabRect( m, kWidths, kCount, 1 ).w );
}

TEST( GlobalHeader, PillsRunLeftToRightAndNeverOverlap )
{
	// [rc4l] The property the whole layout is for. Swept over a range of label widths rather than
	// spot-checked, because overlap only shows up at particular ratios and a screenshot of the two
	// shipped labels would not find it.
	const HeaderMetrics m = DefaultHeaderMetrics( );

	for ( int a = 1; a <= 200; a += 7 )
	{
		for ( int b = 1; b <= 200; b += 11 )
		{
			const int widths[] = { a, b };
			const HeaderRect first = HeaderTabRect( m, widths, 2, 0 );
			const HeaderRect second = HeaderTabRect( m, widths, 2, 1 );

			EXPECT_GE( second.x, first.x + first.w ) << "widths " << a << "," << b;
		}
	}
}

TEST( GlobalHeader, TheGapBetweenPillsIsExactlyTheGap )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );
	const HeaderRect first = HeaderTabRect( m, kWidths, kCount, 0 );
	const HeaderRect second = HeaderTabRect( m, kWidths, kCount, 1 );

	EXPECT_EQ( first.x + first.w + m.gap, second.x );
}

TEST( GlobalHeader, TheFirstPillStartsAtTheLeftPadding )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_EQ( m.leftPad, HeaderTabRect( m, kWidths, kCount, 0 ).x );
}

TEST( GlobalHeader, EveryPillSitsInsideTheBar )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	for ( int i = 0; i < kCount; ++i )
	{
		const HeaderRect r = HeaderTabRect( m, kWidths, kCount, i );
		EXPECT_TRUE( HeaderBarContains( m, r.y )) << "tab " << i;
		EXPECT_TRUE( HeaderBarContains( m, r.y + r.h - 1 )) << "tab " << i;
	}
}

// ------------------------------------------------------------ bad indices

TEST( GlobalHeader, AnIndexOffTheEndIsAnEmptyRectNotAReadPastTheArray )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_EQ( 0, HeaderTabRect( m, kWidths, kCount, -1 ).w );
	EXPECT_EQ( 0, HeaderTabRect( m, kWidths, kCount, kCount ).w );
	EXPECT_EQ( 0, HeaderTabRect( m, kWidths, kCount, 999 ).w );
}

TEST( GlobalHeader, NoWidthsAtAllIsAnEmptyRect )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_EQ( 0, HeaderTabRect( m, 0, 2, 0 ).w );
	EXPECT_EQ( -1, HeaderTabAtPoint( m, 0, 2, 10, 8 ));
}

// -------------------------------------------------------------- hit-test

TEST( GlobalHeader, TheHitTestAgreesWithWhereThePillWasDrawn )
{
	// [rc4l] THE reason this is a unit and not two copies of the arithmetic. Every pixel of every
	// pill is swept, so a hit-test that drifts from the drawing by even one pixel fails here rather
	// than turning into a click that lands on the wrong tab.
	const HeaderMetrics m = DefaultHeaderMetrics( );

	for ( int i = 0; i < kCount; ++i )
	{
		const HeaderRect r = HeaderTabRect( m, kWidths, kCount, i );

		for ( int x = r.x; x < r.x + r.w; ++x )
		{
			for ( int y = r.y; y < r.y + r.h; ++y )
				EXPECT_EQ( i, HeaderTabAtPoint( m, kWidths, kCount, x, y )) << "tab " << i;
		}
	}
}

TEST( GlobalHeader, TheBarBackgroundIsNotAButton )
{
	// Between and around the pills is chrome. Answering a tab there would make the whole top of the
	// screen a click target for something the player cannot see.
	const HeaderMetrics m = DefaultHeaderMetrics( );
	const HeaderRect first = HeaderTabRect( m, kWidths, kCount, 0 );

	EXPECT_EQ( -1, HeaderTabAtPoint( m, kWidths, kCount, 0, 8 ));                    // left of the first
	EXPECT_EQ( -1, HeaderTabAtPoint( m, kWidths, kCount, first.x + 1, 0 ));          // above the pills
	EXPECT_EQ( -1, HeaderTabAtPoint( m, kWidths, kCount, first.x + first.w + 1, 8 )); // in the gap
	EXPECT_EQ( -1, HeaderTabAtPoint( m, kWidths, kCount, 9999, 8 ));                 // off the right
}

TEST( GlobalHeader, BelowTheBarIsTheMenusBusiness )
{
	// Everything under the bar belongs to whatever menu is open. Claiming a point down there would
	// eat clicks meant for a row the player can see and is aiming at.
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_FALSE( HeaderBarContains( m, m.barH ));
	EXPECT_FALSE( HeaderBarContains( m, 200 ));
	EXPECT_FALSE( HeaderBarContains( m, -1 ));
	EXPECT_TRUE( HeaderBarContains( m, 0 ));
}

// ------------------------------------------------------------- traversal

TEST( GlobalHeader, LeftAndRightWalkTheBar )
{
	EXPECT_EQ( 1, StepHeaderTab( 0, kHeaderTabCount, +1 ));
	EXPECT_EQ( 0, StepHeaderTab( 1, kHeaderTabCount, -1 ));
}

TEST( GlobalHeader, TheEndsOfTheBarHold )
{
	// Clamped, not wrapped: with two tabs a wrap makes left and right do the same thing, and then
	// there is no way to feel where the row ends without looking at it.
	EXPECT_EQ( 0, StepHeaderTab( 0, kHeaderTabCount, -1 ));
	EXPECT_EQ( kHeaderTabCount - 1, StepHeaderTab( kHeaderTabCount - 1, kHeaderTabCount, +1 ));
}

TEST( GlobalHeader, TraversalNeverLeavesTheBar )
{
	// Swept including out-of-range starts and oversized steps, because the index is stored across
	// frames and a menu that reloaded under it can hand back anything.
	for ( int start = -3; start <= 5; ++start )
	{
		for ( int step = -4; step <= 4; ++step )
		{
			const int next = StepHeaderTab( start, kHeaderTabCount, step );
			EXPECT_GE( next, 0 ) << "start " << start << " step " << step;
			EXPECT_LT( next, kHeaderTabCount ) << "start " << start << " step " << step;
		}
	}
}

TEST( GlobalHeader, NoTabsMeansNowhereToGo )
{
	EXPECT_EQ( 0, StepHeaderTab( 0, 0, +1 ));
	EXPECT_EQ( 0, StepHeaderTab( 3, -1, -1 ));
}

// ------------------------------------------------ leaving a menu upwards

TEST( GlobalHeader, TheTopRowIsTheFirstOneTheCursorCanReach )
{
	// The shape of nearly every stock menu: a banner, then the entries. Index 1 is the top as far as
	// the player is concerned, and that is where Up has to hand over.
	const bool rows[] = { false, true, true, true };

	EXPECT_TRUE( CursorAtTopRow( rows, 4, 1 ));
	EXPECT_FALSE( CursorAtTopRow( rows, 4, 2 ));
	EXPECT_FALSE( CursorAtTopRow( rows, 4, 3 ));
}

TEST( GlobalHeader, AnUnreachableRowAboveTheCursorDoesNotCount )
{
	// A greyed-out entry between the banner and the first live one. It is drawn, it is skipped, and
	// counting it would leave the player pressing Up into a wrap on a menu that looks like it has a
	// top row.
	const bool rows[] = { false, false, true, true };

	EXPECT_TRUE( CursorAtTopRow( rows, 4, 2 ));
}

TEST( GlobalHeader, ARowTheCursorCannotSitOnIsNotTheTop )
{
	// Happens for real: an entry disables itself while the cursor is on it. Whatever the index says,
	// this is not a position Up should be handing the bar the keyboard from.
	const bool rows[] = { true, false, true };

	EXPECT_FALSE( CursorAtTopRow( rows, 3, 1 ));
}

TEST( GlobalHeader, NothingSelectedKeepsUpForTheMenu )
{
	// -1 is how a menu says "no cursor yet", and its own Up means "pick a row". Taking that key would
	// leave a menu the keyboard could never get into in the first place.
	const bool rows[] = { true, true };

	EXPECT_FALSE( CursorAtTopRow( rows, 2, -1 ));
}

TEST( GlobalHeader, AMenuWithNothingToSelectNeverReportsATop )
{
	const bool rows[] = { false, false };

	EXPECT_FALSE( CursorAtTopRow( rows, 2, 0 ));
	EXPECT_FALSE( CursorAtTopRow( rows, 2, 1 ));
}

TEST( GlobalHeader, AnIndexOffTheEndIsAnswered )
{
	// The selection is held by the menu across reloads, so it can outlive the list it points into.
	const bool rows[] = { true, true };

	EXPECT_FALSE( CursorAtTopRow( rows, 2, 2 ));
	EXPECT_FALSE( CursorAtTopRow( rows, 2, 99 ));
	EXPECT_FALSE( CursorAtTopRow( rows, 0, 0 ));
	EXPECT_FALSE( CursorAtTopRow( 0, 3, 0 ));
}

TEST( GlobalHeader, ExactlyOneRowIsEverTheTop )
{
	// Swept, because "first reachable" is the kind of loop that quietly answers true twice.
	const bool rows[] = { false, true, false, true, true };
	int tops = 0;

	for ( int i = 0; i < 5; ++i )
	{
		if ( CursorAtTopRow( rows, 5, i ))
			++tops;
	}

	EXPECT_EQ( 1, tops );
}
