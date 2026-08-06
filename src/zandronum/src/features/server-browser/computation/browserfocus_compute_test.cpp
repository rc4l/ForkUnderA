// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/browserfocus_compute.h"

using zx::BrowserFocus;
using zx::ComputeNav;
using zx::NavKey;
using zx::NavResult;

namespace
{
const bool kHasRows = true;
const bool kEmpty = false;
const bool kLastTab = true;
const bool kFirstTab = false;

const BrowserFocus kZones[] = {
	BrowserFocus::Tabs, BrowserFocus::Search, BrowserFocus::Rows, BrowserFocus::Action,
};
const NavKey kKeys[] = { NavKey::Up, NavKey::Down, NavKey::Left, NavKey::Right };
const int kZoneCount = 4;
const int kKeyCount = 4;
} // namespace

// ---------------------------------------------------------------- the top row

TEST( BrowserNav, StepsAlongTheTabsWithLeftAndRight )
{
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, kFirstTab ).tabStep );
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows, kLastTab ).tabStep );
}

TEST( BrowserNav, DoesNotWrapOffEitherEndOfTheRow )
{
	// Left on the first tab has nothing to its left; right on the last has the search box, which is a
	// move and not a tab switch. Wrapping either way would make the row a loop with no ends, and the
	// search box the thing the loop skips.
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows, kFirstTab ).tabStep );
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, kLastTab ).tabStep );
}

TEST( BrowserNav, RightOffTheLastTabReachesTheSearchBox )
{
	const NavResult r = ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, kLastTab );
	EXPECT_EQ( BrowserFocus::Search, r.focus );
	EXPECT_EQ( 0, r.tabStep );
}

TEST( BrowserNav, SwitchingTabsKeepsFocusOnTheTabs )
{
	// Otherwise every tab switch would throw you somewhere else, and the second tab would be
	// impossible to reach with the same key that reached the first.
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, kFirstTab ).focus );
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows, kLastTab ).focus );
}

TEST( BrowserNav, LeftAndRightAreNotNavigationInTheSearchBox )
{
	// They belong to the caret. A text field that jumped to another control when you tried to move
	// through what you had typed would be unusable, and this is the one thing a focused field must
	// claim for itself.
	EXPECT_EQ( BrowserFocus::Search,
		ComputeNav( BrowserFocus::Search, NavKey::Left, kHasRows, kLastTab ).focus );
	EXPECT_EQ( BrowserFocus::Search,
		ComputeNav( BrowserFocus::Search, NavKey::Right, kHasRows, kLastTab ).focus );
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Search, NavKey::Left, kHasRows, kLastTab ).tabStep );
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Search, NavKey::Right, kHasRows, kLastTab ).tabStep );
}

TEST( BrowserNav, UpOutOfTheSearchBoxReturnsToTheTabs )
{
	// A single line has nowhere above for the caret to go, so up means what it means everywhere else,
	// and the tabs are the only other thing on this row.
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Search, NavKey::Up, kHasRows, kLastTab ).focus );
}

TEST( BrowserNav, DownAlwaysLetsGoOfTheSearchBox )
{
	// Into the list when there is one, back to the tabs when there is not -- but OUT either way.
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Search, NavKey::Down, kHasRows, kLastTab ).focus );
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Search, NavKey::Down, kEmpty, kLastTab ).focus );
}

TEST( BrowserNav, DownFromEitherEndOfTheRowEntersTheList )
{
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Tabs, NavKey::Down, kHasRows, kFirstTab ).focus );
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Search, NavKey::Down, kHasRows, kLastTab ).focus );
}

TEST( BrowserNav, EnteringTheListDoesNotAlsoStepThroughIt )
{
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Tabs, NavKey::Down, kHasRows, kFirstTab ).rowStep );
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Search, NavKey::Down, kHasRows, kLastTab ).rowStep );
}

TEST( BrowserNav, UpFromTheTabsGoesNowhere )
{
	const NavResult r = ComputeNav( BrowserFocus::Tabs, NavKey::Up, kHasRows, kFirstTab );
	EXPECT_EQ( BrowserFocus::Tabs, r.focus );
	EXPECT_EQ( 0, r.tabStep );
	EXPECT_EQ( 0, r.rowStep );
}

// ---------------------------------------------------------------- the list

TEST( BrowserNav, MovesTheSelectionWithUpAndDown )
{
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Rows, NavKey::Up, kHasRows, kFirstTab ).rowStep );
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Rows, NavKey::Down, kHasRows, kFirstTab ).rowStep );
}

TEST( BrowserNav, MovingTheSelectionDoesNotChangeFocus )
{
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Rows, NavKey::Up, kHasRows, kFirstTab ).focus );
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Rows, NavKey::Down, kHasRows, kFirstTab ).focus );
}

TEST( BrowserNav, RightFromTheListReachesTheButton )
{
	const NavResult r = ComputeNav( BrowserFocus::Rows, NavKey::Right, kHasRows, kFirstTab );
	EXPECT_EQ( BrowserFocus::Action, r.focus );
	EXPECT_EQ( 0, r.rowStep );
	EXPECT_EQ( 0, r.tabStep );		// and emphatically does not also switch tabs, which it used to
}

TEST( BrowserNav, LeftFromTheListDoesNothing )
{
	const NavResult r = ComputeNav( BrowserFocus::Rows, NavKey::Left, kHasRows, kFirstTab );
	EXPECT_EQ( BrowserFocus::Rows, r.focus );
	EXPECT_EQ( 0, r.tabStep );
	EXPECT_EQ( 0, r.rowStep );
}

// ---------------------------------------------------------------- the button

TEST( BrowserNav, LeftFromTheButtonGoesBackToTheList )
{
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Action, NavKey::Left, kHasRows, kFirstTab ).focus );
}

TEST( BrowserNav, UpFromTheButtonGoesBackToTheTabs )
{
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Action, NavKey::Up, kHasRows, kFirstTab ).focus );
}

TEST( BrowserNav, DownAndRightFromTheButtonGoNowhere )
{
	// There is nothing past the button in either direction, and a key that silently wrapped to the
	// far side of the screen is worse than one that does nothing.
	EXPECT_EQ( BrowserFocus::Action,
		ComputeNav( BrowserFocus::Action, NavKey::Down, kHasRows, kFirstTab ).focus );
	EXPECT_EQ( BrowserFocus::Action,
		ComputeNav( BrowserFocus::Action, NavKey::Right, kHasRows, kFirstTab ).focus );
}

TEST( BrowserNav, TheButtonNeverMovesTheSelectionOrTheTab )
{
	for ( int k = 0; k < kKeyCount; ++k )
	{
		const NavResult r = ComputeNav( BrowserFocus::Action, kKeys[k], kHasRows, kFirstTab );
		EXPECT_EQ( 0, r.tabStep ) << k;
		EXPECT_EQ( 0, r.rowStep ) << k;
	}
}

// ---------------------------------------------------------------- an empty list

TEST( BrowserNav, DownFromTheRowStaysPutWhenNothingIsListed )
{
	// Entering an empty list would focus a region with no row to be on, and the detail panel and the
	// JOIN button both read a selection that would not exist. The search box still LETS GO on down --
	// it just does not hand the keyboard to a list that is not there.
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Tabs, NavKey::Down, kEmpty, kFirstTab ).focus );
	EXPECT_NE( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Search, NavKey::Down, kEmpty, kLastTab ).focus );
}

TEST( BrowserNav, TheTopRowStillWorksWhenNothingIsListed )
{
	// The likeliest reasons the list is empty are the wrong tab and too narrow a search, so both have
	// to stay reachable from the screen that reports the problem.
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kEmpty, kFirstTab ).tabStep );
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kEmpty, kLastTab ).tabStep );
	EXPECT_EQ( BrowserFocus::Search,
		ComputeNav( BrowserFocus::Tabs, NavKey::Right, kEmpty, kLastTab ).focus );
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Search, NavKey::Up, kEmpty, kLastTab ).focus );
}

TEST( BrowserNav, AFocusOnRowsFallsBackToTheTabsOnceTheListEmpties )
{
	// Servers time out while the browser is open, so this focus was legitimate when it was set.
	for ( int k = 0; k < kKeyCount; ++k )
	{
		const NavResult r = ComputeNav( BrowserFocus::Rows, kKeys[k], kEmpty, kFirstTab );
		EXPECT_NE( BrowserFocus::Rows, r.focus ) << k;
		EXPECT_EQ( 0, r.rowStep ) << k;
	}

	// And having fallen back, it behaves as the tabs do.
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Rows, NavKey::Right, kEmpty, kFirstTab ).tabStep );
}

TEST( BrowserNav, LeavingTheButtonSkipsTheListWhenItIsEmpty )
{
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Action, NavKey::Left, kEmpty, kFirstTab ).focus );
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Action, NavKey::Up, kEmpty, kFirstTab ).focus );
}

// ---------------------------------------------------------------- the loop as a whole

TEST( BrowserNav, WalksTheWholeLoopAndBack )
{
	// Tabs -> search -> list -> button -> list -> tabs, in one go.
	BrowserFocus f = BrowserFocus::Tabs;

	f = ComputeNav( f, NavKey::Right, kHasRows, kLastTab ).focus;
	EXPECT_EQ( BrowserFocus::Search, f );

	f = ComputeNav( f, NavKey::Down, kHasRows, kLastTab ).focus;
	EXPECT_EQ( BrowserFocus::Rows, f );

	f = ComputeNav( f, NavKey::Right, kHasRows, kLastTab ).focus;
	EXPECT_EQ( BrowserFocus::Action, f );

	f = ComputeNav( f, NavKey::Left, kHasRows, kLastTab ).focus;
	EXPECT_EQ( BrowserFocus::Rows, f );

	f = ComputeNav( f, NavKey::Right, kHasRows, kLastTab ).focus;
	f = ComputeNav( f, NavKey::Up, kHasRows, kLastTab ).focus;
	EXPECT_EQ( BrowserFocus::Tabs, f );
}

TEST( BrowserNav, EverySettingIsReachableFromEveryOther )
{
	// A zone nothing can reach is a control nobody can use. Breadth-first over the whole graph.
	for ( int start = 0; start < kZoneCount; ++start )
	{
		bool seen[kZoneCount] = { false, false, false, false };
		BrowserFocus frontier[16];
		int count = 0;

		frontier[count++] = kZones[start];
		seen[start] = true;

		for ( int i = 0; i < count; ++i )
		{
			for ( int k = 0; k < kKeyCount; ++k )
				for ( int lastTab = 0; lastTab < 2; ++lastTab )
				{
					const BrowserFocus next =
						ComputeNav( frontier[i], kKeys[k], kHasRows, lastTab != 0 ).focus;

					for ( int z = 0; z < kZoneCount; ++z )
					{
						if (( kZones[z] == next ) && !seen[z] )
						{
							seen[z] = true;
							frontier[count++] = next;
						}
					}
				}
		}

		for ( int z = 0; z < kZoneCount; ++z )
			EXPECT_TRUE( seen[z] ) << "zone " << z << " unreachable from zone " << start;
	}
}

TEST( BrowserNav, NeverMovesBothTheTabAndTheSelection )
{
	// One key, one meaning. If this ever fired, the caller would be applying a tab switch and a row
	// step from the same press, on a list the tab switch just replaced.
	for ( int z = 0; z < kZoneCount; ++z )
		for ( int k = 0; k < kKeyCount; ++k )
			for ( int e = 0; e < 2; ++e )
				for ( int lastTab = 0; lastTab < 2; ++lastTab )
				{
					const NavResult r = ComputeNav( kZones[z], kKeys[k], e != 0, lastTab != 0 );

					EXPECT_FALSE(( r.tabStep != 0 ) && ( r.rowStep != 0 )) << z << "," << k;
					EXPECT_GE( r.tabStep, -1 );
					EXPECT_LE( r.tabStep, 1 );
					EXPECT_GE( r.rowStep, -1 );
					EXPECT_LE( r.rowStep, 1 );
				}
}

TEST( BrowserNav, AKeyThatMovesSomethingLeavesFocusWhereItWas )
{
	for ( int z = 0; z < kZoneCount; ++z )
		for ( int k = 0; k < kKeyCount; ++k )
			for ( int lastTab = 0; lastTab < 2; ++lastTab )
			{
				const NavResult r = ComputeNav( kZones[z], kKeys[k], kHasRows, lastTab != 0 );
				if (( r.tabStep != 0 ) || ( r.rowStep != 0 ))
					EXPECT_EQ( kZones[z], r.focus ) << z << "," << k;
			}
}
