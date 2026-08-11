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

	Session &InGame( bool yes )
	{
		in.inGame = yes;
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

// ------------------------------------------------------------- in a game

TEST( MenuResume, BeingInAGameNeverOpensTheBrowser )
{
	// The Join Game report: Escape in a game means the in-game menu, the one thing on screen that
	// can get the player out again. A server list, to somebody already on a server, is not that.
	Session s;
	s.Showing( MenuSection::Browser ).InGame( true ).Closed( );

	EXPECT_EQ( MenuSection::MainMenu, s.Opens( ));
}

TEST( MenuResume, LeavingTheGameRestoresTheBrowserTheyWereOn )
{
	// The guard is about being in a game, not about forgetting. Someone who browsed, joined, and
	// then quit back out should still find the list where they left it.
	Session s;
	s.Showing( MenuSection::Browser ).InGame( true );
	EXPECT_EQ( MenuSection::MainMenu, s.Opens( ));

	s.InGame( false );
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

TEST( MenuResume, AFinishedJoinBeatsBeingInAGameToo )
{
	// A download can finish while the player is still connected somewhere else, and the band is on
	// screen saying so. Refusing here would leave an instruction the player cannot follow.
	Session s;
	s.Showing( MenuSection::MainMenu ).InGame( true ).JoinReady( true );

	EXPECT_EQ( MenuSection::Browser, s.Opens( ));
}

TEST( MenuResume, TheDefaultSessionOpensOnTheMainMenu )
{
	// A cold start has looked at nothing, joined nothing and is in nothing.
	EXPECT_EQ( MenuSection::MainMenu, Session( ).Opens( ));
}
