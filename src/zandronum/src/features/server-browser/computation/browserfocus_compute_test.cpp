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
// [rc4l] A three-tab row -- PUBLIC, PRIVATE, HOST -- because that is what the browser has, and
// because the middle one is the position the old two-tab bool could not describe.
const int kTabCount = 3;
const int kFirstTab = 0;
const int kMiddleTab = 1;
const int kLastTab = 2;

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
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, kFirstTab, kTabCount ).tabStep );
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows, kLastTab, kTabCount ).tabStep );
}

TEST( BrowserNav, DoesNotWrapOffEitherEndOfTheRow )
{
	// Left on the first tab has nothing to its left; right on the last has the search box, which is a
	// move and not a tab switch. Wrapping either way would make the row a loop with no ends, and the
	// search box the thing the loop skips.
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows, kFirstTab, kTabCount ).tabStep );
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, kLastTab, kTabCount ).tabStep );
}

TEST( BrowserNav, RightOffTheLastTabReachesTheSearchBox )
{
	const NavResult r = ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, kLastTab, kTabCount );
	EXPECT_EQ( BrowserFocus::Search, r.focus );
	EXPECT_EQ( 0, r.tabStep );
}

TEST( BrowserNav, SwitchingTabsKeepsFocusOnTheTabs )
{
	// Otherwise every tab switch would throw you somewhere else, and the second tab would be
	// impossible to reach with the same key that reached the first.
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, kFirstTab, kTabCount ).focus );
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows, kLastTab, kTabCount ).focus );
}

TEST( BrowserNav, LeftAndRightAreNotNavigationInTheSearchBox )
{
	// They belong to the caret. A text field that jumped to another control when you tried to move
	// through what you had typed would be unusable, and this is the one thing a focused field must
	// claim for itself.
	EXPECT_EQ( BrowserFocus::Search,
		ComputeNav( BrowserFocus::Search, NavKey::Left, kHasRows, kLastTab, kTabCount ).focus );
	EXPECT_EQ( BrowserFocus::Search,
		ComputeNav( BrowserFocus::Search, NavKey::Right, kHasRows, kLastTab, kTabCount ).focus );
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Search, NavKey::Left, kHasRows, kLastTab, kTabCount ).tabStep );
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Search, NavKey::Right, kHasRows, kLastTab, kTabCount ).tabStep );
}

TEST( BrowserNav, UpOutOfTheSearchBoxReturnsToTheTabs )
{
	// A single line has nowhere above for the caret to go, so up means what it means everywhere else,
	// and the tabs are the only other thing on this row.
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Search, NavKey::Up, kHasRows, kLastTab, kTabCount ).focus );
}

TEST( BrowserNav, DownAlwaysLetsGoOfTheSearchBox )
{
	// Into the list when there is one, back to the tabs when there is not -- but OUT either way.
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Search, NavKey::Down, kHasRows, kLastTab, kTabCount ).focus );
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Search, NavKey::Down, kEmpty, kLastTab, kTabCount ).focus );
}

TEST( BrowserNav, DownFromEitherEndOfTheRowEntersTheList )
{
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Tabs, NavKey::Down, kHasRows, kFirstTab, kTabCount ).focus );
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Search, NavKey::Down, kHasRows, kLastTab, kTabCount ).focus );
}

TEST( BrowserNav, EnteringTheListDoesNotAlsoStepThroughIt )
{
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Tabs, NavKey::Down, kHasRows, kFirstTab, kTabCount ).rowStep );
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Search, NavKey::Down, kHasRows, kLastTab, kTabCount ).rowStep );
}

TEST( BrowserNav, UpFromTheTabsGoesNowhere )
{
	const NavResult r = ComputeNav( BrowserFocus::Tabs, NavKey::Up, kHasRows, kFirstTab, kTabCount );
	EXPECT_EQ( BrowserFocus::Tabs, r.focus );
	EXPECT_EQ( 0, r.tabStep );
	EXPECT_EQ( 0, r.rowStep );
}

// ---------------------------------------------------------------- the list

TEST( BrowserNav, MovesTheSelectionWithUpAndDown )
{
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Rows, NavKey::Up, kHasRows, kFirstTab, kTabCount ).rowStep );
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Rows, NavKey::Down, kHasRows, kFirstTab, kTabCount ).rowStep );
}

TEST( BrowserNav, MovingTheSelectionDoesNotChangeFocus )
{
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Rows, NavKey::Up, kHasRows, kFirstTab, kTabCount ).focus );
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Rows, NavKey::Down, kHasRows, kFirstTab, kTabCount ).focus );
}

TEST( BrowserNav, RightFromTheListReachesTheButton )
{
	const NavResult r = ComputeNav( BrowserFocus::Rows, NavKey::Right, kHasRows, kFirstTab, kTabCount );
	EXPECT_EQ( BrowserFocus::Action, r.focus );
	EXPECT_EQ( 0, r.rowStep );
	EXPECT_EQ( 0, r.tabStep );		// and emphatically does not also switch tabs, which it used to
}

TEST( BrowserNav, LeftFromTheListDoesNothing )
{
	const NavResult r = ComputeNav( BrowserFocus::Rows, NavKey::Left, kHasRows, kFirstTab, kTabCount );
	EXPECT_EQ( BrowserFocus::Rows, r.focus );
	EXPECT_EQ( 0, r.tabStep );
	EXPECT_EQ( 0, r.rowStep );
}

// ---------------------------------------------------------------- the button

TEST( BrowserNav, LeftFromTheButtonGoesBackToTheList )
{
	EXPECT_EQ( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Action, NavKey::Left, kHasRows, kFirstTab, kTabCount ).focus );
}

TEST( BrowserNav, UpFromTheButtonGoesBackToTheTabs )
{
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Action, NavKey::Up, kHasRows, kFirstTab, kTabCount ).focus );
}

TEST( BrowserNav, DownAndRightFromTheButtonGoNowhere )
{
	// There is nothing past the button in either direction, and a key that silently wrapped to the
	// far side of the screen is worse than one that does nothing.
	EXPECT_EQ( BrowserFocus::Action,
		ComputeNav( BrowserFocus::Action, NavKey::Down, kHasRows, kFirstTab, kTabCount ).focus );
	EXPECT_EQ( BrowserFocus::Action,
		ComputeNav( BrowserFocus::Action, NavKey::Right, kHasRows, kFirstTab, kTabCount ).focus );
}

TEST( BrowserNav, TheButtonNeverMovesTheSelectionOrTheTab )
{
	for ( int k = 0; k < kKeyCount; ++k )
	{
		const NavResult r = ComputeNav( BrowserFocus::Action, kKeys[k], kHasRows, kFirstTab, kTabCount );
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
		ComputeNav( BrowserFocus::Tabs, NavKey::Down, kEmpty, kFirstTab, kTabCount ).focus );
	EXPECT_NE( BrowserFocus::Rows,
		ComputeNav( BrowserFocus::Search, NavKey::Down, kEmpty, kLastTab, kTabCount ).focus );
}

TEST( BrowserNav, TheTopRowStillWorksWhenNothingIsListed )
{
	// The likeliest reasons the list is empty are the wrong tab and too narrow a search, so both have
	// to stay reachable from the screen that reports the problem.
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kEmpty, kFirstTab, kTabCount ).tabStep );
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kEmpty, kLastTab, kTabCount ).tabStep );
	EXPECT_EQ( BrowserFocus::Search,
		ComputeNav( BrowserFocus::Tabs, NavKey::Right, kEmpty, kLastTab, kTabCount ).focus );
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Search, NavKey::Up, kEmpty, kLastTab, kTabCount ).focus );
}

TEST( BrowserNav, AFocusOnRowsFallsBackToTheTabsOnceTheListEmpties )
{
	// Servers time out while the browser is open, so this focus was legitimate when it was set.
	for ( int k = 0; k < kKeyCount; ++k )
	{
		const NavResult r = ComputeNav( BrowserFocus::Rows, kKeys[k], kEmpty, kFirstTab, kTabCount );
		EXPECT_NE( BrowserFocus::Rows, r.focus ) << k;
		EXPECT_EQ( 0, r.rowStep ) << k;
	}

	// And having fallen back, it behaves as the tabs do.
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Rows, NavKey::Right, kEmpty, kFirstTab, kTabCount ).tabStep );
}

TEST( BrowserNav, LeavingTheButtonSkipsTheListWhenItIsEmpty )
{
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Action, NavKey::Left, kEmpty, kFirstTab, kTabCount ).focus );
	EXPECT_EQ( BrowserFocus::Tabs,
		ComputeNav( BrowserFocus::Action, NavKey::Up, kEmpty, kFirstTab, kTabCount ).focus );
}

// ---------------------------------------------------------------- the loop as a whole

TEST( BrowserNav, WalksTheWholeLoopAndBack )
{
	// Tabs -> search -> list -> button -> list -> tabs, in one go.
	BrowserFocus f = BrowserFocus::Tabs;

	f = ComputeNav( f, NavKey::Right, kHasRows, kLastTab, kTabCount ).focus;
	EXPECT_EQ( BrowserFocus::Search, f );

	f = ComputeNav( f, NavKey::Down, kHasRows, kLastTab, kTabCount ).focus;
	EXPECT_EQ( BrowserFocus::Rows, f );

	f = ComputeNav( f, NavKey::Right, kHasRows, kLastTab, kTabCount ).focus;
	EXPECT_EQ( BrowserFocus::Action, f );

	f = ComputeNav( f, NavKey::Left, kHasRows, kLastTab, kTabCount ).focus;
	EXPECT_EQ( BrowserFocus::Rows, f );

	f = ComputeNav( f, NavKey::Right, kHasRows, kLastTab, kTabCount ).focus;
	f = ComputeNav( f, NavKey::Up, kHasRows, kLastTab, kTabCount ).focus;
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
				for ( int lastTab = 0; lastTab < kTabCount; ++lastTab )
				{
					const BrowserFocus next =
						ComputeNav( frontier[i], kKeys[k], kHasRows, lastTab, kTabCount ).focus;

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
				for ( int lastTab = 0; lastTab < kTabCount; ++lastTab )
				{
					const NavResult r = ComputeNav( kZones[z], kKeys[k], e != 0, lastTab, kTabCount );

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
			for ( int lastTab = 0; lastTab < kTabCount; ++lastTab )
			{
				const NavResult r = ComputeNav( kZones[z], kKeys[k], kHasRows, lastTab, kTabCount );
				if (( r.tabStep != 0 ) || ( r.rowStep != 0 ))
					EXPECT_EQ( kZones[z], r.focus ) << z << "," << k;
			}
}

// ---------------------------------------------------------------- the modal

TEST( BrowserNav, ADialogIsModalOnEveryKey )
{
	// A dialog is a question, and the arrows belong to it while it is up. Letting one of them walk the
	// focus back into the browser would leave the panel on screen with the highlight somewhere behind
	// it -- the player arrowing over controls they cannot reach, and a question nobody is answering.
	// What happens INSIDE the dialog is dialog_compute's job; this only has to refuse to leave.
	for ( int k = 0; k < kKeyCount; ++k )
		for ( int e = 0; e < 2; ++e )
			for ( int lastTab = 0; lastTab < kTabCount; ++lastTab )
			{
				const NavResult r = ComputeNav( BrowserFocus::Dialog, kKeys[k], e != 0, lastTab, kTabCount );

				EXPECT_EQ( BrowserFocus::Dialog, r.focus ) << k;
				EXPECT_EQ( 0, r.tabStep ) << k;
				EXPECT_EQ( 0, r.rowStep ) << k;
			}
}

// ---------------------------------------------------------------- the hosting form

TEST( BrowserNav, TheHostingFormKeepsItsOwnArrows )
{
	// [rc4l] Up and down walk the form's fields, and only the caller knows how many there are; left
	// and right belong to the caret in whichever field is focused, exactly as in the search box. So
	// this unit refuses all four rather than guessing -- a form that jumped to another region halfway
	// through a port number would be unusable.
	for ( int k = 0; k < kKeyCount; ++k )
		for ( int e = 0; e < 2; ++e )
		{
			const NavResult r = ComputeNav( BrowserFocus::Host, kKeys[k], e != 0, kFirstTab, kTabCount );

			EXPECT_EQ( BrowserFocus::Host, r.focus ) << k;
			EXPECT_EQ( 0, r.tabStep ) << k;
			EXPECT_EQ( 0, r.rowStep ) << k;
		}
}

// ---------------------------------------------------------------- a row longer than two

TEST( BrowserNav, AMiddleTabCanGoBothWays )
{
	// [rc4l] THE BUG THIS SIGNATURE CHANGE EXISTS FOR, and it shipped.
	//
	// The tab argument used to be a single bool, onLastTab, and the header argued that one bit spared
	// this unit from having to know how many tabs there were. True of two. False of three: the middle
	// tab is neither the first nor the last, and "am I on the last one" cannot answer LEFT for it.
	//
	// PRIVATE could step right to HOST and could not step left to PUBLIC, because the only question
	// being asked was one the middle of a row does not answer.
	EXPECT_EQ( -1, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows, kMiddleTab,
		kTabCount ).tabStep );
	EXPECT_EQ( 1, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, kMiddleTab,
		kTabCount ).tabStep );
}

TEST( BrowserNav, EveryTabCanReachItsNeighbours )
{
	// Swept over the whole row rather than at the two ends, which is where the old bool was tested
	// and is exactly where a three-tab row is least interesting.
	for ( int at = 0; at < kTabCount; ++at )
	{
		const NavResult left = ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows, at, kTabCount );
		const NavResult right = ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, at, kTabCount );

		// Left moves unless there is nothing to the left of it.
		EXPECT_EQ(( at > 0 ) ? -1 : 0, left.tabStep ) << at;

		// Right moves along the row until it runs out, and then steps off into the search box.
		if ( at < kTabCount - 1 )
		{
			EXPECT_EQ( 1, right.tabStep ) << at;
			EXPECT_EQ( BrowserFocus::Tabs, right.focus ) << at;
		}
		else
		{
			EXPECT_EQ( 0, right.tabStep ) << at;
			EXPECT_EQ( BrowserFocus::Search, right.focus ) << at;
		}
	}
}

TEST( BrowserNav, ARowOfOneTabHasNowhereToStep )
{
	// A count the browser does not currently use, checked because the unit now takes one and a
	// caller that ever passes it must not get a step into a tab that is not there.
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Tabs, NavKey::Left, kHasRows, 0, 1 ).tabStep );
	EXPECT_EQ( 0, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, 0, 1 ).tabStep );
	EXPECT_EQ( BrowserFocus::Search, ComputeNav( BrowserFocus::Tabs, NavKey::Right, kHasRows, 0,
		1 ).focus );
}
