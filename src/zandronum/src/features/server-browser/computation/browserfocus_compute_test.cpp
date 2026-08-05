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
} // namespace

// ---------------------------------------------------------------- the tabs

TEST( BrowserNav, SwitchesTabsWithLeftAndRight )
{
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows ).tabStep );
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows ).tabStep );
}

TEST( BrowserNav, SwitchingTabsKeepsFocusOnTheTabs )
{
	// Otherwise every tab switch would throw you into the list, and the second one would be
	// impossible to reach with the same key that reached the first.
	EXPECT_EQ( BrowserFocus::Tabs, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows ).focus );
	EXPECT_EQ( BrowserFocus::Tabs, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows ).focus );
}

TEST( BrowserNav, DownFromTheTabsEntersTheList )
{
	const NavResult r = ComputeNav( BrowserFocus::Tabs, NavKey::Down, kHasRows );
	EXPECT_EQ( BrowserFocus::Rows, r.focus );
	EXPECT_EQ( 0, r.rowStep );	// entering lands on whatever is selected; it does not also step
}

TEST( BrowserNav, UpFromTheTabsGoesNowhere )
{
	// The tabs are the top of the loop. Up is the key that gets you back TO them.
	const NavResult r = ComputeNav( BrowserFocus::Tabs, NavKey::Up, kHasRows );
	EXPECT_EQ( BrowserFocus::Tabs, r.focus );
	EXPECT_EQ( 0, r.tabStep );
	EXPECT_EQ( 0, r.rowStep );
}

// ---------------------------------------------------------------- the list

TEST( BrowserNav, MovesTheSelectionWithUpAndDown )
{
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Rows, NavKey::Up, kHasRows ).rowStep );
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Rows, NavKey::Down, kHasRows ).rowStep );
}

TEST( BrowserNav, MovingTheSelectionDoesNotChangeFocus )
{
	EXPECT_EQ( BrowserFocus::Rows, ComputeNav( BrowserFocus::Rows, NavKey::Up, kHasRows ).focus );
	EXPECT_EQ( BrowserFocus::Rows, ComputeNav( BrowserFocus::Rows, NavKey::Down, kHasRows ).focus );
}

TEST( BrowserNav, RightFromTheListReachesTheButton )
{
	const NavResult r = ComputeNav( BrowserFocus::Rows, NavKey::Right, kHasRows );
	EXPECT_EQ( BrowserFocus::Action, r.focus );
	EXPECT_EQ( 0, r.rowStep );
	EXPECT_EQ( 0, r.tabStep );	// and emphatically does not also switch tabs, which is what it used to do
}

TEST( BrowserNav, LeftFromTheListDoesNothing )
{
	const NavResult r = ComputeNav( BrowserFocus::Rows, NavKey::Left, kHasRows );
	EXPECT_EQ( BrowserFocus::Rows, r.focus );
	EXPECT_EQ( 0, r.tabStep );
	EXPECT_EQ( 0, r.rowStep );
}

// ---------------------------------------------------------------- the button

TEST( BrowserNav, LeftFromTheButtonGoesBackToTheList )
{
	EXPECT_EQ( BrowserFocus::Rows, ComputeNav( BrowserFocus::Action, NavKey::Left, kHasRows ).focus );
}

TEST( BrowserNav, UpFromTheButtonGoesBackToTheTabs )
{
	EXPECT_EQ( BrowserFocus::Tabs, ComputeNav( BrowserFocus::Action, NavKey::Up, kHasRows ).focus );
}

TEST( BrowserNav, DownAndRightFromTheButtonGoNowhere )
{
	// There is nothing past the button in either direction, and a key that silently wrapped to the
	// far side of the screen is worse than one that does nothing.
	EXPECT_EQ( BrowserFocus::Action, ComputeNav( BrowserFocus::Action, NavKey::Down, kHasRows ).focus );
	EXPECT_EQ( BrowserFocus::Action, ComputeNav( BrowserFocus::Action, NavKey::Right, kHasRows ).focus );
}

TEST( BrowserNav, TheButtonNeverMovesTheSelectionOrTheTab )
{
	const NavKey keys[] = { NavKey::Up, NavKey::Down, NavKey::Left, NavKey::Right };
	for ( int i = 0; i < 4; ++i )
	{
		const NavResult r = ComputeNav( BrowserFocus::Action, keys[i], kHasRows );
		EXPECT_EQ( 0, r.tabStep ) << i;
		EXPECT_EQ( 0, r.rowStep ) << i;
	}
}

// ---------------------------------------------------------------- an empty list

TEST( BrowserNav, DownFromTheTabsStaysPutWhenNothingIsListed )
{
	// Entering an empty list would focus a region with no row to be on, and the detail panel and the
	// JOIN button both read a selection that would not exist.
	const NavResult r = ComputeNav( BrowserFocus::Tabs, NavKey::Down, kEmpty );
	EXPECT_EQ( BrowserFocus::Tabs, r.focus );
	EXPECT_EQ( 0, r.rowStep );
}

TEST( BrowserNav, TabsStillSwitchWhenNothingIsListed )
{
	// The likeliest reason the list is empty is that you are on the wrong tab.
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kEmpty ).tabStep );
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kEmpty ).tabStep );
}

TEST( BrowserNav, AFocusOnRowsFallsBackToTheTabsOnceTheListEmpties )
{
	// Servers time out while the browser is open, so this focus was legitimate when it was set.
	const NavKey keys[] = { NavKey::Up, NavKey::Down, NavKey::Left, NavKey::Right };
	for ( int i = 0; i < 4; ++i )
	{
		const NavResult r = ComputeNav( BrowserFocus::Rows, keys[i], kEmpty );
		EXPECT_NE( BrowserFocus::Rows, r.focus ) << i;
		EXPECT_EQ( 0, r.rowStep ) << i;
	}

	// And having fallen back, it behaves as the tabs do -- left and right switch.
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Rows, NavKey::Left, kEmpty ).tabStep );
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Rows, NavKey::Right, kEmpty ).tabStep );
}

TEST( BrowserNav, LeavingTheButtonSkipsTheListWhenItIsEmpty )
{
	EXPECT_EQ( BrowserFocus::Tabs, ComputeNav( BrowserFocus::Action, NavKey::Left, kEmpty ).focus );
	EXPECT_EQ( BrowserFocus::Tabs, ComputeNav( BrowserFocus::Action, NavKey::Up, kEmpty ).focus );
}

// ---------------------------------------------------------------- the loop as a whole

TEST( BrowserNav, WalksTheWholeLoopAndBack )
{
	// The flow as described, in one go: tabs -> list -> button -> list -> tabs.
	BrowserFocus f = BrowserFocus::Tabs;

	f = ComputeNav( f, NavKey::Down, kHasRows ).focus;
	EXPECT_EQ( BrowserFocus::Rows, f );

	f = ComputeNav( f, NavKey::Right, kHasRows ).focus;
	EXPECT_EQ( BrowserFocus::Action, f );

	f = ComputeNav( f, NavKey::Left, kHasRows ).focus;
	EXPECT_EQ( BrowserFocus::Rows, f );

	// And the other way out of the button.
	f = ComputeNav( f, NavKey::Right, kHasRows ).focus;
	f = ComputeNav( f, NavKey::Up, kHasRows ).focus;
	EXPECT_EQ( BrowserFocus::Tabs, f );
}

TEST( BrowserNav, NeverMovesBothTheTabAndTheSelection )
{
	// One key, one meaning. If this ever fired, the caller would be applying a tab switch and a row
	// step from the same press, on a list that the tab switch just replaced.
	const BrowserFocus zones[] = { BrowserFocus::Tabs, BrowserFocus::Rows, BrowserFocus::Action };
	const NavKey keys[] = { NavKey::Up, NavKey::Down, NavKey::Left, NavKey::Right };

	for ( int z = 0; z < 3; ++z )
	{
		for ( int k = 0; k < 4; ++k )
		{
			for ( int e = 0; e < 2; ++e )
			{
				const NavResult r = ComputeNav( zones[z], keys[k], e != 0 );

				EXPECT_FALSE(( r.tabStep != 0 ) && ( r.rowStep != 0 )) << z << "," << k << "," << e;
				EXPECT_GE( r.tabStep, -1 );
				EXPECT_LE( r.tabStep, 1 );
				EXPECT_GE( r.rowStep, -1 );
				EXPECT_LE( r.rowStep, 1 );

				// A key that moved something must have left focus where it was.
				if (( r.tabStep != 0 ) || ( r.rowStep != 0 ))
					EXPECT_EQ(( zones[z] == BrowserFocus::Rows ) && ( e == 0 ) ? BrowserFocus::Tabs : zones[z],
						r.focus ) << z << "," << k << "," << e;
			}
		}
	}
}
