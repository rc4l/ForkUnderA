// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/global-header/computation/globalheader_compute.h"

#include <stdlib.h>

using zx::DefaultHeaderMetrics;
using zx::HeaderBarContains;
using zx::HeaderMetrics;
using zx::HeaderRect;
using zx::HeaderRowLeft;
using zx::HeaderRowWidth;
using zx::HeaderTabAtPoint;
using zx::HeaderTabRect;
using zx::StepHeaderTabPinned;
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

TEST( GlobalHeader, ARowOfNothingIsWideEnoughForNothing )
{
	// The same guard HeaderTabRect has, for the same reason: a caller that got the widths wrong is
	// exactly the caller that will not check, and reading past the array to measure a row that is not
	// there is a worse answer than zero.
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_EQ( 0, HeaderRowWidth( m, 0, kCount , -1));
	EXPECT_EQ( 0, HeaderRowWidth( m, kWidths, 0 , -1));
	EXPECT_EQ( 0, HeaderRowWidth( m, kWidths, -1 , -1));
}

TEST( GlobalHeader, TheRowCentresOnTheBarOnceTheBarHasSaidHowWideItIs )
{
	HeaderMetrics m = DefaultHeaderMetrics( );
	m.barW = 1000;

	const int rowW = HeaderRowWidth( m, kWidths, kCount , -1);
	const int left = HeaderRowLeft( m, kWidths, kCount , -1);

	// Equal air either side, to within the pixel an odd remainder costs.
	EXPECT_LE( abs(( m.barW - ( left + rowW )) - left ), 1 );
}

TEST( GlobalHeader, ARowTooWideToCentreCrowdsTheMiddleRatherThanItsOwnOrb )
{
	// leftPad is not spare margin, it is the room the first tab's orb needs. A bar too narrow to
	// centre in must still not push its own marker off the screen.
	HeaderMetrics m = DefaultHeaderMetrics( );
	m.barW = 10;

	EXPECT_EQ( m.leftPad, HeaderRowLeft( m, kWidths, kCount , -1));
}

TEST( GlobalHeader, ABarThatNeverSaidHowWideItIsGetsTheLeftEdge )
{
	// Zero is a caller that has not opted in. Centring on a width of nothing would put the row at a
	// negative x, which is a worse answer than the one it replaced.
	HeaderMetrics m = DefaultHeaderMetrics( );
	m.barW = 0;

	EXPECT_EQ( m.leftPad, HeaderRowLeft( m, kWidths, kCount , -1));
}

TEST( GlobalHeader, TheRowIsAsWideAsThePillsAndTheGapsBetweenThem )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	int expected = 0;
	for ( int i = 0; i < kCount; ++i )
	{
		expected += kWidths[i] + 2 * m.labelPad;
		if ( i > 0 )
			expected += m.gap;
	}

	EXPECT_EQ( expected, HeaderRowWidth( m, kWidths, kCount , -1));

	// And it agrees with the rects: the last pill's right edge is the row's.
	HeaderMetrics wide = m;
	wide.barW = 1000;
	const HeaderRect last = HeaderTabRect( wide, kWidths, kCount, kCount - 1 , -1);
	EXPECT_EQ( HeaderRowLeft( wide, kWidths, kCount , -1) + expected, last.x + last.w );
}

TEST( GlobalHeader, TheFirstTabsFocusOrbFitsOnTheScreen )
{
	// The bug: the orb hangs to the LEFT of the pill it marks, and the lead-in was smaller than the
	// orb, so the marker on the first tab was drawn half off the side of the screen. Nothing else on
	// the bar could show this, because only the first tab has the screen edge for a neighbour.
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_GE( m.leftPad - m.glowInset, m.glowRadius );
}

TEST( GlobalHeader, TheGapBetweenTabsHoldsAnOrbWithoutLandingOnTheOneBefore )
{
	// Same arithmetic, one neighbour along. An orb that overflows the gap points at the previous tab,
	// which is worse than being clipped: it is legible and wrong.
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_GE( m.gap - m.glowInset, m.glowRadius );
}

TEST( GlobalHeader, EveryTabsOrbClearsTheOneBeforeIt )
{
	// Swept over the real layout rather than asserted on the gap alone, because the pills are sized
	// to their labels: the spacing that matters is the one the rects actually end up with.
	const HeaderMetrics m = DefaultHeaderMetrics( );

	for ( int i = 0; i < kCount; ++i )
	{
		const HeaderRect r = HeaderTabRect( m, kWidths, kCount, i , -1);
		const int orbLeft = r.x - m.glowInset - m.glowRadius;

		if ( i == 0 )
		{
			EXPECT_GE( orbLeft, 0 ) << "tab " << i;
			continue;
		}

		const HeaderRect prev = HeaderTabRect( m, kWidths, kCount, i - 1 , -1);
		EXPECT_GE( orbLeft, prev.x + prev.w ) << "tab " << i;
	}
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
	// Swept over both dials, because the whole point of deriving it is that they are going to move.
	// The bar's height in the menus' own units is barH * zoom / 200; anything less than that is a bar
	// drawn over a menu, which is the failure this exists to make impossible.
	for ( int zoom = 25; zoom <= 300; zoom += 25 )
	{
		for ( int barH = 0; barH <= 200; ++barH )
		{
			HeaderMetrics m = DefaultHeaderMetrics( );
			m.barH = barH;
			m.zoomPercent = zoom;

			EXPECT_GE( MenuClearanceY( m ) * 200, barH * zoom )
				<< "barH " << barH << " zoom " << zoom;
		}
	}
}

TEST( GlobalHeader, TurningTheDialUpMovesTheMenusFurtherDown )
{
	// The dial has to reach the clearance, or a bigger bar would be drawn over menus that stayed put.
	HeaderMetrics small = DefaultHeaderMetrics( );
	small.zoomPercent = 100;

	HeaderMetrics big = DefaultHeaderMetrics( );
	big.zoomPercent = 150;

	EXPECT_GT( MenuClearanceY( big ), MenuClearanceY( small ));
}

TEST( GlobalHeader, ADialOfNothingIsReadAsNoZoomRatherThanAsZeroSize )
{
	// Zero would otherwise collapse the clearance to nothing and put the bar straight over the menu.
	HeaderMetrics none = DefaultHeaderMetrics( );
	none.zoomPercent = 0;

	HeaderMetrics plain = DefaultHeaderMetrics( );
	plain.zoomPercent = 100;

	EXPECT_EQ( MenuClearanceY( plain ), MenuClearanceY( none ));
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

	EXPECT_EQ( kWidths[0] + 2 * m.labelPad, HeaderTabRect( m, kWidths, kCount, 0 , -1).w );
	EXPECT_EQ( kWidths[1] + 2 * m.labelPad, HeaderTabRect( m, kWidths, kCount, 1 , -1).w );
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
			const HeaderRect first = HeaderTabRect( m, widths, 2, 0 , -1);
			const HeaderRect second = HeaderTabRect( m, widths, 2, 1 , -1);

			EXPECT_GE( second.x, first.x + first.w ) << "widths " << a << "," << b;
		}
	}
}

TEST( GlobalHeader, TheGapBetweenPillsIsExactlyTheGap )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );
	const HeaderRect first = HeaderTabRect( m, kWidths, kCount, 0 , -1);
	const HeaderRect second = HeaderTabRect( m, kWidths, kCount, 1 , -1);

	EXPECT_EQ( first.x + first.w + m.gap, second.x );
}

TEST( GlobalHeader, TheFirstPillStartsAtTheLeftPadding )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_EQ( m.leftPad, HeaderTabRect( m, kWidths, kCount, 0 , -1).x );
}

TEST( GlobalHeader, EveryPillSitsInsideTheBar )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	for ( int i = 0; i < kCount; ++i )
	{
		const HeaderRect r = HeaderTabRect( m, kWidths, kCount, i , -1);
		EXPECT_TRUE( HeaderBarContains( m, r.y )) << "tab " << i;
		EXPECT_TRUE( HeaderBarContains( m, r.y + r.h - 1 )) << "tab " << i;
	}
}

// ------------------------------------------------------------ bad indices

TEST( GlobalHeader, AnIndexOffTheEndIsAnEmptyRectNotAReadPastTheArray )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_EQ( 0, HeaderTabRect( m, kWidths, kCount, -1 , -1).w );
	EXPECT_EQ( 0, HeaderTabRect( m, kWidths, kCount, kCount , -1).w );
	EXPECT_EQ( 0, HeaderTabRect( m, kWidths, kCount, 999 , -1).w );
}

TEST( GlobalHeader, NoWidthsAtAllIsAnEmptyRect )
{
	const HeaderMetrics m = DefaultHeaderMetrics( );

	EXPECT_EQ( 0, HeaderTabRect( m, 0, 2, 0 , -1).w );
	EXPECT_EQ( -1, HeaderTabAtPoint( m, 0, 2, 10, 8 , -1));
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
		const HeaderRect r = HeaderTabRect( m, kWidths, kCount, i , -1);

		for ( int x = r.x; x < r.x + r.w; ++x )
		{
			for ( int y = r.y; y < r.y + r.h; ++y )
				EXPECT_EQ( i, HeaderTabAtPoint( m, kWidths, kCount, x, y , -1)) << "tab " << i;
		}
	}
}

TEST( GlobalHeader, TheBarBackgroundIsNotAButton )
{
	// Between and around the pills is chrome. Answering a tab there would make the whole top of the
	// screen a click target for something the player cannot see.
	const HeaderMetrics m = DefaultHeaderMetrics( );
	const HeaderRect first = HeaderTabRect( m, kWidths, kCount, 0 , -1);

	EXPECT_EQ( -1, HeaderTabAtPoint( m, kWidths, kCount, 0, 8 , -1));                    // left of the first
	EXPECT_EQ( -1, HeaderTabAtPoint( m, kWidths, kCount, first.x + 1, 0 , -1));          // above the pills
	EXPECT_EQ( -1, HeaderTabAtPoint( m, kWidths, kCount, first.x + first.w + 1, 8 , -1)); // in the gap
	EXPECT_EQ( -1, HeaderTabAtPoint( m, kWidths, kCount, 9999, 8 , -1));                 // off the right
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

// ---------------------------------------------------------------- the pinned tab

namespace
{

// Continue is last in the enum and first on the screen; these are its label widths alongside the
// two that are always there.
const int kPinnedWidths[3] = { 60, 90, 70 };	// Main Menu, Play Online!, Continue
const int kPinned = 2;

zx::HeaderMetrics WideBar()
{
	zx::HeaderMetrics m = zx::DefaultHeaderMetrics();
	m.barW = 640;
	return m;
}

} // namespace

TEST( HeaderPinned, ThePinnedTabSitsAtTheLeftEdge )
{
	const zx::HeaderMetrics m = WideBar();
	const zx::HeaderRect r = HeaderTabRect( m, kPinnedWidths, 3, kPinned, kPinned );

	EXPECT_EQ( m.leftPad, r.x );
	EXPECT_EQ( kPinnedWidths[kPinned] + 2 * m.labelPad, r.w );
}

TEST( HeaderPinned, TheOtherTabsDoNotMoveWhenItAppears )
{
	// The whole reason Continue was appended to the enum rather than inserted: the two tabs that are
	// always there must stay exactly where the player last saw them.
	const zx::HeaderMetrics m = WideBar();

	const zx::HeaderRect mainWithout = HeaderTabRect( m, kPinnedWidths, 2, 0, -1 );
	const zx::HeaderRect onlineWithout = HeaderTabRect( m, kPinnedWidths, 2, 1, -1 );

	const zx::HeaderRect mainWith = HeaderTabRect( m, kPinnedWidths, 3, 0, kPinned );
	const zx::HeaderRect onlineWith = HeaderTabRect( m, kPinnedWidths, 3, 1, kPinned );

	EXPECT_EQ( mainWithout.x, mainWith.x );
	EXPECT_EQ( onlineWithout.x, onlineWith.x );
}

TEST( HeaderPinned, ThePinnedPillIsNotMeasuredAsPartOfTheCentredRow )
{
	const zx::HeaderMetrics m = WideBar();
	EXPECT_EQ( HeaderRowWidth( m, kPinnedWidths, 2, -1 ),
		HeaderRowWidth( m, kPinnedWidths, 3, kPinned ));
}

TEST( HeaderPinned, TheCentredRowNeverSlidesUnderThePinnedPill )
{
	// Two pills sharing pixels is a click that hits whichever was drawn last.
	zx::HeaderMetrics m = WideBar();
	m.barW = 200;			// too narrow to centre a row this wide

	const zx::HeaderRect pin = HeaderTabRect( m, kPinnedWidths, 3, kPinned, kPinned );
	const zx::HeaderRect first = HeaderTabRect( m, kPinnedWidths, 3, 0, kPinned );

	EXPECT_GE( first.x, pin.x + pin.w ) << "the centred row started inside the pinned pill";
}

TEST( HeaderPinned, ClickingThePinnedPillFindsIt )
{
	const zx::HeaderMetrics m = WideBar();
	const zx::HeaderRect r = HeaderTabRect( m, kPinnedWidths, 3, kPinned, kPinned );

	EXPECT_EQ( kPinned, HeaderTabAtPoint( m, kPinnedWidths, 3, r.x + 1, r.y + 1, kPinned ));
	EXPECT_EQ( kPinned, HeaderTabAtPoint( m, kPinnedWidths, 3, r.x + r.w - 1, r.y + r.h - 1, kPinned ));
}

TEST( HeaderPinned, ClickingBesideThePinnedPillFindsNothing )
{
	const zx::HeaderMetrics m = WideBar();
	const zx::HeaderRect r = HeaderTabRect( m, kPinnedWidths, 3, kPinned, kPinned );

	EXPECT_EQ( -1, HeaderTabAtPoint( m, kPinnedWidths, 3, r.x + r.w + 1, r.y + 1, kPinned ));
}

TEST( HeaderPinned, TheArrowsStepInTheOrderTheEyeSees )
{
	// Left to right that is Continue, Main Menu, Play Online -- which is NOT the enum order.
	EXPECT_EQ( 0, StepHeaderTabPinned( kPinned, 3, kPinned, +1 ));
	EXPECT_EQ( 1, StepHeaderTabPinned( 0, 3, kPinned, +1 ));
	EXPECT_EQ( 0, StepHeaderTabPinned( 1, 3, kPinned, -1 ));
	EXPECT_EQ( kPinned, StepHeaderTabPinned( 0, 3, kPinned, -1 ));
}

TEST( HeaderPinned, TheEndsOfTheBarStillClamp )
{
	// Same promise as the unpinned bar: a bar whose ends you cannot feel is one you have to look at.
	EXPECT_EQ( kPinned, StepHeaderTabPinned( kPinned, 3, kPinned, -1 ));
	EXPECT_EQ( 1, StepHeaderTabPinned( 1, 3, kPinned, +1 ));
}

TEST( HeaderPinned, SteppingWithNothingPinnedIsTheOrdinaryWalk )
{
	for ( int i = 0; i < 2; ++i )
	{
		EXPECT_EQ( StepHeaderTab( i, 2, +1 ), StepHeaderTabPinned( i, 2, -1, +1 ));
		EXPECT_EQ( StepHeaderTab( i, 2, -1 ), StepHeaderTabPinned( i, 2, -1, -1 ));
	}
}

TEST( HeaderPinned, EveryTabIsReachableFromEveryOtherByArrowsAlone )
{
	// The property that matters: no pill can be stranded where the keyboard cannot get to it.
	for ( int start = 0; start < 3; ++start )
	{
		for ( int target = 0; target < 3; ++target )
		{
			int at = start;
			for ( int guard = 0; guard < 8 && at != target; ++guard )
				at = StepHeaderTabPinned( at, 3, kPinned, ( target == at ) ? 0 : +1 );

			if ( at != target )
			{
				at = start;
				for ( int guard = 0; guard < 8 && at != target; ++guard )
					at = StepHeaderTabPinned( at, 3, kPinned, -1 );
			}

			EXPECT_EQ( target, at ) << "could not walk from " << start << " to " << target;
		}
	}
}

TEST( HeaderPinned, AnOutOfRangePinnedIndexIsIgnoredRatherThanRead )
{
	const zx::HeaderMetrics m = WideBar();
	EXPECT_EQ( HeaderTabRect( m, kPinnedWidths, 2, 0, -1 ).x,
		HeaderTabRect( m, kPinnedWidths, 2, 0, 9 ).x );
	EXPECT_EQ( StepHeaderTab( 0, 2, +1 ), StepHeaderTabPinned( 0, 2, 9, +1 ));
}

TEST( HeaderPinned, ATabAfterThePinnedOneStillLandsInTheRightPlace )
{
	// Continue happens to be last today, so nothing is ever laid out past it. Pinning something in
	// the middle is what proves the walk actually skips the pinned pill rather than getting the
	// right answer because it never had to.
	const zx::HeaderMetrics m = WideBar();
	const int pinnedFirst = 0;

	const zx::HeaderRect second = HeaderTabRect( m, kPinnedWidths, 3, 1, pinnedFirst );
	const zx::HeaderRect third = HeaderTabRect( m, kPinnedWidths, 3, 2, pinnedFirst );

	EXPECT_EQ( second.x + second.w + m.gap, third.x );
	EXPECT_EQ( m.leftPad, HeaderTabRect( m, kPinnedWidths, 3, pinnedFirst, pinnedFirst ).x );
}

TEST( HeaderPinned, AnEmptyBarStepsNowhere )
{
	EXPECT_EQ( 0, StepHeaderTabPinned( 0, 0, -1, +1 ));
	EXPECT_EQ( 0, StepHeaderTabPinned( 3, 0, 1, -1 ));
}
