// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/serversearch_compute.h"

using zx::SearchKey;
using zx::ServerMatchesSearch;
using std::string;

namespace
{
const string ESC( 1, '\034' );

bool Matches( const string &name, const string &typed )
{
	return ServerMatchesSearch( name, SearchKey( typed ));
}
} // namespace

// ---------------------------------------------------------------- casing

TEST( ServerSearch, IgnoresTheCaseOfWhatWasTyped )
{
	// "asdF" and "asdf" are the same search. Stated in the request, and the reason SearchKey exists.
	EXPECT_TRUE( Matches( "asdf server", "asdF" ));
	EXPECT_TRUE( Matches( "asdf server", "ASDF" ));
	EXPECT_TRUE( Matches( "asdf server", "AsDf" ));
}

TEST( ServerSearch, IgnoresTheCaseOfTheServerName )
{
	EXPECT_TRUE( Matches( "BRUTAL DOOM", "brutal" ));
	EXPECT_TRUE( Matches( "Brutal Doom", "BRUTAL" ));
}

TEST( ServerSearch, FoldsOnlyLettersInTheKey )
{
	EXPECT_EQ( "123", SearchKey( "123" ));
	EXPECT_EQ( "a-b_c", SearchKey( "A-B_C" ));
	EXPECT_EQ( "", SearchKey( "" ));
}

// ---------------------------------------------------------------- substring

TEST( ServerSearch, MatchesAnywhereInTheName )
{
	EXPECT_TRUE( Matches( "the 123 server", "123" ));
	EXPECT_TRUE( Matches( "123 server", "123" ));
	EXPECT_TRUE( Matches( "server 123", "123" ));
}

TEST( ServerSearch, DoesNotMatchWhatIsNotThere )
{
	EXPECT_FALSE( Matches( "dwango5", "123" ));
	EXPECT_FALSE( Matches( "", "123" ));
}

TEST( ServerSearch, AnEmptyBoxMatchesEverything )
{
	// Not a filter that excludes everything -- the absence of a filter.
	EXPECT_TRUE( Matches( "anything at all", "" ));
	EXPECT_TRUE( Matches( "", "" ));
}

// ---------------------------------------------------------------- colour codes

TEST( ServerSearch, MatchesWhatIsOnScREENNotWhatIsOnTheWire )
{
	// The name reads "Brutal Doom" but the bytes are not that. A player can only type what they can
	// see, so that is what has to be matched.
	const string coloured = ESC + "dBrutal " + ESC + "hDoom";

	EXPECT_TRUE( Matches( coloured, "brutal doom" ));
	EXPECT_TRUE( Matches( coloured, "Brutal" ));
	EXPECT_TRUE( Matches( coloured, "doom" ));
}

TEST( ServerSearch, DoesNotMatchTheColourCodesThemselves )
{
	// "d" from "\034d" must not be findable: it is not on screen.
	const string coloured = ESC + "[Gold]Alpha";

	EXPECT_TRUE( Matches( coloured, "alpha" ));
	EXPECT_FALSE( Matches( coloured, "gold" ));
}

TEST( ServerSearch, SurvivesATruncatedColourCode )
{
	// Something a server can send, deliberately or not.
	EXPECT_TRUE( Matches( "ab" + ESC, "ab" ));
	EXPECT_FALSE( Matches( ESC + "[unterminated", "unterminated" ));
}

// ---------------------------------------------------------------- shape

TEST( ServerSearch, MatchingIsUnaffectedByHowTheQueryWasCased )
{
	// Every casing of the same query must select the same set, or the list would reshuffle as the
	// player held shift.
	const char *const names[] = { "Alpha", "beta", "GAMMA", "\034ddelta" };
	const char *const queries[] = { "a", "A", "ta", "TA", "Ga", "gA" };

	for ( size_t q = 0; q < sizeof( queries ) / sizeof( queries[0] ); q += 2 )
		for ( size_t n = 0; n < sizeof( names ) / sizeof( names[0] ); ++n )
			EXPECT_EQ( Matches( names[n], queries[q] ), Matches( names[n], queries[q + 1] ))
				<< names[n] << " / " << queries[q];
}

TEST( ServerSearch, LongerQueriesNeverMatchMoreThanTheirPrefixes )
{
	// Typing another character can only narrow the list. If it ever widened it, the list would jump
	// about as the player typed.
	const char *const names[] = { "brutal doom", "brutality", "doom 2", "dwango5" };
	const char *const growing[] = { "b", "br", "bru", "brut", "brutal", "brutal " };

	for ( size_t n = 0; n < sizeof( names ) / sizeof( names[0] ); ++n )
		for ( size_t q = 1; q < sizeof( growing ) / sizeof( growing[0] ); ++q )
		{
			if ( Matches( names[n], growing[q] ))
				EXPECT_TRUE( Matches( names[n], growing[q - 1] ))
					<< names[n] << " matched '" << growing[q] << "' but not '" << growing[q - 1] << "'";
		}
}
