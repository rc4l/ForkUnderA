// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/global-header/computation/menuresume_compute.h"

using zx::ComputeMenuToOpen;
using zx::MenuResumeIn;
using zx::MenuSection;

namespace
{

// [rc4l] The caller's half of the rule, mirrored here so a test can spell a whole session out as the
// player would live it: what they looked at, then what they pressed. Every bug in this logic so far
// has been an ORDER of events rather than a single wrong answer, and a helper that only takes the
// final state cannot express an order.
struct Session
{
	MenuResumeIn in;

	// What the drawing does every frame a menu is up.
	Session &Showing( MenuSection section )
	{
		in.lastShown = section;
		return *this;
	}

	Session &JoinReady( bool yes )
	{
		in.joinReady = yes;
		return *this;
	}

	// Closing changes nothing on its own, which is the whole point of observing instead of hooking.
	Session &Closed( )
	{
		return *this;
	}

	MenuSection Opens( ) const
	{
		return ComputeMenuToOpen( in );
	}
};

} // namespace

// -------------------------------------------------- the orders that broke it

TEST( MenuResume, LeavingTheBrowserAndComingStraightBackReturnsToIt )
{
	// The behaviour this feature exists for.
	Session s;
	s.Showing( MenuSection::Browser ).Closed( );

	EXPECT_EQ( MenuSection::Browser, s.Opens( ));
}

TEST( MenuResume, WalkingFromTheBrowserToTheMainMenuFirstReturnsToTheMainMenu )
{
	// THE REGRESSION, twice over. The player used the tab bar to leave the browser, which replaces
	// the menu without closing anything, so a version that recorded the section at close time never
	// heard about it and handed the browser back on the next Escape.
	Session s;
	s.Showing( MenuSection::Browser )
	 .Showing( MenuSection::MainMenu )
	 .Closed( );

	EXPECT_EQ( MenuSection::MainMenu, s.Opens( ));
}

TEST( MenuResume, TheMainMenuOnItsOwnOpensOnTheMainMenu )
{
	Session s;
	s.Showing( MenuSection::MainMenu ).Closed( );

	EXPECT_EQ( MenuSection::MainMenu, s.Opens( ));
}

TEST( MenuResume, HoppingBackAndForthEndsWhereverItActuallyEnded )
{
	// Swept over a run of tab switches, because "the last one wins" is the entire contract and an
	// implementation that remembered the FIRST, or any, browser visit would pass the tests above.
	for ( int visits = 1; visits <= 8; ++visits )
	{
		Session s;
		for ( int i = 0; i < visits; ++i )
		{
			s.Showing( MenuSection::Browser );
			s.Showing( MenuSection::MainMenu );
		}
		s.Closed( );

		EXPECT_EQ( MenuSection::MainMenu, s.Opens( )) << "visits " << visits;

		Session t;
		for ( int i = 0; i < visits; ++i )
		{
			t.Showing( MenuSection::MainMenu );
			t.Showing( MenuSection::Browser );
		}
		t.Closed( );

		EXPECT_EQ( MenuSection::Browser, t.Opens( )) << "visits " << visits;
	}
}

TEST( MenuResume, BeingInAGameIsNotAReasonToForget )
{
	// There used to be a guard here refusing the browser while in a game. It was the wrong fix for an
	// Escape bug whose cause was elsewhere, and all it did afterwards was overrule the player:
	// somebody hosting a duel, who had been on the browser, was sent to the main menu because of
	// where they were rather than what they had chosen.
	Session s;
	s.Showing( MenuSection::Browser ).Closed( );

	EXPECT_EQ( MenuSection::Browser, s.Opens( ));
}

// --------------------------------------------------------- a finished join

TEST( MenuResume, AFinishedJoinBeatsWhereverTheyWere )
{
	// The band said "Open the Menu" and promised the browser. Nothing on this screen outranks a
	// promise the player has already read.
	Session s;
	s.Showing( MenuSection::MainMenu ).JoinReady( true );

	EXPECT_EQ( MenuSection::Browser, s.Opens( ));
}

TEST( MenuResume, AFinishedJoinOutranksWhereTheyWereEvenOnTheMainMenu )
{
	// A download can finish while the player is somewhere else entirely, and the band is on screen
	// saying so. Refusing here would leave an instruction the player cannot follow.
	Session s;
	s.Showing( MenuSection::MainMenu ).JoinReady( true );

	EXPECT_EQ( MenuSection::Browser, s.Opens( ));
}

TEST( MenuResume, TheDefaultSessionOpensOnTheMainMenu )
{
	// A cold start has looked at nothing, joined nothing and is in nothing.
	EXPECT_EQ( MenuSection::MainMenu, Session( ).Opens( ));
}
