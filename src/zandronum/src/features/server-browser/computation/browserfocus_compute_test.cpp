// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/browserfocus_compute.h"

using zx::BrowserFocus;
using zx::ComputeNav;
using zx::NavKey;
using zx::NavResult;
using zx::NavWhere;

namespace
{

// The browser as shipped: two tabs on top (PLAY, BROWSE) and two sub-tabs under BROWSE (Public,
// Private) with the search box past the end of them.
const int kTabCount = 2;
const int kPlayTab = 0;
const int kBrowseTab = 1;

const int kSubCount = 2;
const int kPublic = 0;
const int kPrivate = 1;

// On BROWSE, with servers listed.
NavWhere Browsing( int sub = kPublic )
{
	return NavWhere( true, kBrowseTab, kTabCount, sub, kSubCount );
}

// On BROWSE, but nothing came back.
NavWhere BrowsingEmpty( int sub = kPublic )
{
	return NavWhere( false, kBrowseTab, kTabCount, sub, kSubCount );
}

// On PLAY, which has a form rather than a list, so no sub-tabs and no rows.
NavWhere Playing( )
{
	return NavWhere( false, kPlayTab, kTabCount, 0, 0 );
}

// Where a focus actually settles before a key is applied. A region that has gone away hands over to
// the nearest one that has not, and the key then acts from THERE, so the sweep below has to compare
// against this rather than against the zone it asked for.
BrowserFocus Settled( BrowserFocus zone, const NavWhere &where )
{
	if ( !where.hasRows && ( zone == BrowserFocus::Rows ))
		return ( where.subCount > 0 ) ? BrowserFocus::SubTabs : BrowserFocus::Tabs;

	if (( where.subCount <= 0 ) && ( zone == BrowserFocus::SubTabs ))
		return BrowserFocus::Tabs;

	return zone;
}

const BrowserFocus kZones[] = {
	BrowserFocus::Tabs, BrowserFocus::SubTabs, BrowserFocus::Search, BrowserFocus::Rows,
	BrowserFocus::Action,
};
const NavKey kKeys[] = { NavKey::Up, NavKey::Down, NavKey::Left, NavKey::Right };
const int kZoneCount = 5;
const int kKeyCount = 4;

} // namespace

// ---------------------------------------------------------------- the top row

TEST( BrowserNav, StepsAlongTheTabsWithLeftAndRight )
{
	// PLAY is index 0 and BROWSE index 1, so right moves off PLAY and left moves off BROWSE.
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Tabs, NavKey::Right, Playing( ) ).tabStep );
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Tabs, NavKey::Left, Browsing( ) ).tabStep );
}

TEST( BrowserNav, DoesNotWrapOffEitherEndOfTheTopRow )
{
	// Right off BROWSE, the last tab, stays put. Nothing sits past it any more: the search box moved
	// down to the row it filters.
	const NavResult right = ComputeNav( BrowserFocus::Tabs, NavKey::Right, Browsing( ) );
	EXPECT_EQ( 0, right.tabStep );
	EXPECT_EQ( BrowserFocus::Tabs, right.focus ) << "and it must not fall through to the search box";

	// Left off PLAY, the first tab, likewise.
	const NavResult left = ComputeNav( BrowserFocus::Tabs, NavKey::Left, Playing( ) );
	EXPECT_EQ( 0, left.tabStep );
	EXPECT_EQ( BrowserFocus::Tabs, left.focus );
}

TEST( BrowserNav, StepsBackFromBrowseToPlay )
{
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Tabs, NavKey::Left, Browsing( ) ).tabStep );
}

TEST( BrowserNav, DownFromTheTabsEntersTheSubTabs )
{
	EXPECT_EQ( BrowserFocus::SubTabs,
		ComputeNav( BrowserFocus::Tabs, NavKey::Down, Browsing( ) ).focus );
}

TEST( BrowserNav, DownFromTheTabsSkipsASubRowThatIsNotThere )
{
	// PLAY has a form, not a list, so there is no sub-tab row to land on. Focus stays put and the
	// caller hands down to the hosting form.
	EXPECT_EQ( BrowserFocus::Tabs, ComputeNav( BrowserFocus::Tabs, NavKey::Down, Playing( ) ).focus );
}

TEST( BrowserNav, ATabWithNoSubRowButWithRowsGoesStraightToTheList )
{
	// Not a shipped combination today, but the rule has to be stated: skipping the sub-row must not
	// mean skipping the list as well.
	const NavWhere where( true, kPlayTab, kTabCount, 0, 0 );
	EXPECT_EQ( BrowserFocus::Rows, ComputeNav( BrowserFocus::Tabs, NavKey::Down, where ).focus );
}

// ---------------------------------------------------------------- the sub-tab row

TEST( BrowserNav, StepsAlongTheSubTabsWithLeftAndRight )
{
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::SubTabs, NavKey::Right, Browsing( kPublic ) ).subStep );
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::SubTabs, NavKey::Left, Browsing( kPrivate ) ).subStep );
}

TEST( BrowserNav, DoesNotWrapOffTheStartOfTheSubRow )
{
	const NavResult r = ComputeNav( BrowserFocus::SubTabs, NavKey::Left, Browsing( kPublic ) );
	EXPECT_EQ( 0, r.subStep );
	EXPECT_EQ( BrowserFocus::SubTabs, r.focus );
}

TEST( BrowserNav, RightOffTheLastSubTabReachesTheSearchBox )
{
	// The reason the sub-tabs must not wrap: the box is a stop on the same row, and looping among
	// them would leave it unreachable from the keyboard.
	const NavResult r = ComputeNav( BrowserFocus::SubTabs, NavKey::Right, Browsing( kPrivate ) );
	EXPECT_EQ( 0, r.subStep );
	EXPECT_EQ( BrowserFocus::Search, r.focus );
}

TEST( BrowserNav, UpFromTheSubTabsReturnsToTheTabs )
{
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::SubTabs, NavKey::Up, Browsing( ) ).focus );
}

TEST( BrowserNav, DownFromTheSubTabsEntersTheList )
{
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::SubTabs, NavKey::Down, Browsing( ) ).focus );
}

TEST( BrowserNav, DownFromTheSubTabsStaysPutWhenNothingIsListed )
{
	EXPECT_EQ( BrowserFocus::SubTabs,
		ComputeNav( BrowserFocus::SubTabs, NavKey::Down, BrowsingEmpty( ) ).focus );
}

TEST( BrowserNav, ASubTabFocusOnATabWithoutOneAnswersAsTheTabs )
{
	// Switching to PLAY pulls the sub-row out from under the focus. Every key must still answer
	// something the caller can act on.
	for ( int k = 0; k < kKeyCount; ++k )
	{
		const NavResult r = ComputeNav( BrowserFocus::SubTabs, kKeys[k], Playing( ) );
		EXPECT_NE( BrowserFocus::SubTabs, r.focus )
			<< "key " << k << " left focus on a row that is not drawn";
		EXPECT_EQ( 0, r.subStep ) << "key " << k << " stepped along a row that is not there";
	}
}

// ---------------------------------------------------------------- the search box

TEST( BrowserNav, TheSearchBoxKeepsLeftAndRightForItsCaret )
{
	const NavResult left = ComputeNav( BrowserFocus::Search, NavKey::Left, Browsing( ) );
	EXPECT_EQ( BrowserFocus::Search, left.focus );
	EXPECT_EQ( 0, left.subStep );
	EXPECT_EQ( 0, left.tabStep );

	const NavResult right = ComputeNav( BrowserFocus::Search, NavKey::Right, Browsing( ) );
	EXPECT_EQ( BrowserFocus::Search, right.focus );
}

TEST( BrowserNav, UpFromTheSearchBoxReturnsToTheSubTabs )
{
	// Its own row, not the one above it. Going to the top row would skip a whole row of controls on
	// the way out of a box you reached by walking along that row.
	EXPECT_EQ( BrowserFocus::SubTabs,
		ComputeNav( BrowserFocus::Search, NavKey::Up, Browsing( ) ).focus );
}

TEST( BrowserNav, DownFromTheSearchBoxEntersTheList )
{
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Search, NavKey::Down, Browsing( ) ).focus );
}

TEST( BrowserNav, DownFromTheSearchBoxFallsBackToTheSubTabsWhenEmpty )
{
	// Typing a filter that matches nothing must not strand the focus below the last row.
	EXPECT_EQ( BrowserFocus::SubTabs,
		ComputeNav( BrowserFocus::Search, NavKey::Down, BrowsingEmpty( ) ).focus );
}

// ---------------------------------------------------------------- the list

TEST( BrowserNav, MovesTheSelectionWithUpAndDown )
{
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Rows, NavKey::Up, Browsing( ) ).rowStep );
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Rows, NavKey::Down, Browsing( ) ).rowStep );
}

TEST( BrowserNav, MovingTheSelectionDoesNotChangeFocus )
{
	// The rule that stops the caller inventing its own answer for the overlap.
	EXPECT_EQ( BrowserFocus::Rows, ComputeNav( BrowserFocus::Rows, NavKey::Down, Browsing( ) ).focus );
}

TEST( BrowserNav, RightFromTheListReachesTheButton )
{
	EXPECT_EQ( BrowserFocus::Action,
		ComputeNav( BrowserFocus::Rows, NavKey::Right, Browsing( ) ).focus );
}

TEST( BrowserNav, LeftFromTheListDoesNothing )
{
	const NavResult r = ComputeNav( BrowserFocus::Rows, NavKey::Left, Browsing( ) );
	EXPECT_EQ( BrowserFocus::Rows, r.focus );
	EXPECT_EQ( 0, r.rowStep );
}

TEST( BrowserNav, AnEmptyListIsNotEnterableAndNotStayable )
{
	// The list can empty out under a focus that was legitimate when it was set.
	for ( int k = 0; k < kKeyCount; ++k )
	{
		const NavResult r = ComputeNav( BrowserFocus::Rows, kKeys[k], BrowsingEmpty( ) );
		EXPECT_NE( BrowserFocus::Rows, r.focus ) << "key " << k;
		EXPECT_EQ( 0, r.rowStep ) << "key " << k << " moved a selection that does not exist";
	}
}

// ---------------------------------------------------------------- the button

TEST( BrowserNav, LeftFromTheButtonReturnsToTheList )
{
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Action, NavKey::Left, Browsing( ) ).focus );
}

TEST( BrowserNav, UpFromTheButtonReturnsToTheSubTabs )
{
	EXPECT_EQ( BrowserFocus::SubTabs,
		ComputeNav( BrowserFocus::Action, NavKey::Up, Browsing( ) ).focus );
}

TEST( BrowserNav, LeavingTheButtonSkipsAnEmptyList )
{
	EXPECT_EQ( BrowserFocus::SubTabs,
		ComputeNav( BrowserFocus::Action, NavKey::Left, BrowsingEmpty( ) ).focus );
}

TEST( BrowserNav, UpFromTheButtonReachesTheTabsWhenThereIsNoSubRow )
{
	const NavWhere where( true, kPlayTab, kTabCount, 0, 0 );
	EXPECT_EQ( BrowserFocus::Tabs, ComputeNav( BrowserFocus::Action, NavKey::Up, where ).focus );
}

// ---------------------------------------------------------------- modal and the form

TEST( BrowserNav, TheDialogIsModalAndNothingLeavesIt )
{
	for ( int k = 0; k < kKeyCount; ++k )
	{
		const NavResult r = ComputeNav( BrowserFocus::Dialog, kKeys[k], Browsing( ) );
		EXPECT_EQ( BrowserFocus::Dialog, r.focus ) << "key " << k << " escaped a modal dialog";
		EXPECT_EQ( 0, r.rowStep );
		EXPECT_EQ( 0, r.tabStep );
		EXPECT_EQ( 0, r.subStep );
	}
}

TEST( BrowserNav, TheHostFormOwnsItsOwnArrowsHere )
{
	// This unit answers nothing for the form: the caller walks its fields, because only it knows how
	// many there are.
	for ( int k = 0; k < kKeyCount; ++k )
	{
		const NavResult r = ComputeNav( BrowserFocus::Host, kKeys[k], Playing( ) );
		EXPECT_EQ( BrowserFocus::Host, r.focus ) << "key " << k;
		EXPECT_EQ( 0, r.rowStep );
		EXPECT_EQ( 0, r.tabStep );
		EXPECT_EQ( 0, r.subStep );
	}
}

// ---------------------------------------------------------------- invariants

TEST( BrowserNav, EveryZoneAndKeyAnswersSomethingUsable )
{
	// The sweep that catches a zone added without a case: no key may both move something and change
	// focus, and no answer may point at a region that is not currently occupiable.
	const NavWhere cases[] = { Browsing( kPublic ), Browsing( kPrivate ), BrowsingEmpty( ), Playing( ) };

	for ( int c = 0; c < 4; ++c )
	{
		for ( int z = 0; z < kZoneCount; ++z )
		{
			for ( int k = 0; k < kKeyCount; ++k )
			{
				const NavResult r = ComputeNav( kZones[z], kKeys[k], cases[c] );

				const int moves = ( r.tabStep != 0 ) + ( r.subStep != 0 ) + ( r.rowStep != 0 );
				EXPECT_LE( moves, 1 ) << "case " << c << " zone " << z << " key " << k
					<< " moved two things at once";

				if ( moves > 0 )
				{
					EXPECT_EQ( Settled( kZones[z], cases[c] ), r.focus )
						<< "case " << c << " zone " << z << " key " << k
						<< " moved something AND changed focus";
				}

				if ( r.focus == BrowserFocus::Rows )
					EXPECT_TRUE( cases[c].hasRows ) << "case " << c << " landed on an empty list";

				if ( r.focus == BrowserFocus::SubTabs )
					EXPECT_GT( cases[c].subCount, 0 ) << "case " << c << " landed on a missing sub-row";
			}
		}
	}
}

TEST( BrowserNav, TheTabsAreAlwaysReachableGoingUp )
{
	// Whatever else changes, there must be a way back to the top with the keyboard alone.
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::SubTabs, NavKey::Up, Browsing( ) ).focus );
	EXPECT_EQ( BrowserFocus::SubTabs,
		ComputeNav( BrowserFocus::Search, NavKey::Up, Browsing( ) ).focus );
	EXPECT_EQ( BrowserFocus::SubTabs,
		ComputeNav( BrowserFocus::Action, NavKey::Up, Browsing( ) ).focus );
}
