// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/timeago_compute.h"

#include <string>

using zx::LastRefreshedLine;
using zx::TimeAgo;

// ------------------------------------------------------------------ units

TEST( TimeAgo, SecondsUpToAMinute )
{
	EXPECT_EQ( "1 sec ago", TimeAgo( 1 ));
	EXPECT_EQ( "5 secs ago", TimeAgo( 5 ));
	EXPECT_EQ( "59 secs ago", TimeAgo( 59 ));
}

TEST( TimeAgo, MinutesUpToAnHour )
{
	EXPECT_EQ( "1 min ago", TimeAgo( 60 ));
	EXPECT_EQ( "5 mins ago", TimeAgo( 300 ));
	EXPECT_EQ( "59 mins ago", TimeAgo( 3599 ));
}

TEST( TimeAgo, HoursUpToADay )
{
	EXPECT_EQ( "1 hour ago", TimeAgo( 3600 ));
	EXPECT_EQ( "3 hours ago", TimeAgo( 3600 * 3 ));
	EXPECT_EQ( "23 hours ago", TimeAgo( 86399 ));
}

TEST( TimeAgo, DaysAfterThat )
{
	EXPECT_EQ( "1 day ago", TimeAgo( 86400 ));
	EXPECT_EQ( "2 days ago", TimeAgo( 86400 * 2 ));
}

// ------------------------------------------------------------ never two

TEST( TimeAgo, NeverMixesTwoUnits )
{
	// [rc4l] The rule the whole unit exists for. "1 hour 5 mins ago" is precision nobody asked for
	// in a string nobody can scan. Swept across a full day rather than spot-checked.
	static const char *const units[] = { "sec", "min", "hour", "day" };

	for ( int seconds = 0; seconds <= 86400 * 3; seconds += 37 )
	{
		const std::string text = TimeAgo( seconds );

		int found = 0;
		for ( int u = 0; u < 4; ++u )
		{
			if ( text.find( units[u] ) != std::string::npos )
				++found;
		}

		// "sec" is not a substring of any other unit here, nor "min", "hour", "day" of each other.
		EXPECT_LE( found, 1 ) << "seconds " << seconds << " -> " << text;
	}
}

TEST( TimeAgo, IsAlwaysShort )
{
	// It sits on a tooltip. A line that wraps is a line nobody reads.
	for ( int seconds = 0; seconds <= 86400 * 400; seconds += 997 )
		EXPECT_LT( TimeAgo( seconds ).size( ), static_cast<size_t>( 24 )) << "seconds " << seconds;
}

// ------------------------------------------------------------- rounding

TEST( TimeAgo, RoundsDownSoTheNumberIsNeverLargerThanTheTruth )
{
	// A tooltip that says "3 mins" at 2:01 overstates how stale the list is, and this is read to
	// decide whether to trust what is on screen.
	EXPECT_EQ( "2 mins ago", TimeAgo( 179 ));
	EXPECT_EQ( "1 hour ago", TimeAgo( 7199 ));
	EXPECT_EQ( "1 day ago", TimeAgo( 172799 ));
}

TEST( TimeAgo, SingularIsSpeltOut )
{
	// A tooltip is prose, and "1 mins" is the kind of thing a reader trips over.
	EXPECT_EQ( std::string::npos, TimeAgo( 1 ).find( "secs" ));
	EXPECT_EQ( std::string::npos, TimeAgo( 60 ).find( "mins" ));
	EXPECT_EQ( std::string::npos, TimeAgo( 3600 ).find( "hours" ));
	EXPECT_EQ( std::string::npos, TimeAgo( 86400 ).find( "days" ));
}

// ------------------------------------------------------------ edge cases

TEST( TimeAgo, ThisInstantIsJustNow )
{
	// Zero of anything is a number pretending to be a fact.
	EXPECT_EQ( "just now", TimeAgo( 0 ));
}

TEST( TimeAgo, ABackwardsClockIsNotTheFuture )
{
	EXPECT_EQ( "at an unknown time", TimeAgo( -1 ));
	EXPECT_EQ( "at an unknown time", TimeAgo( -100000 ));
}

// ----------------------------------------------------------------- line

TEST( TimeAgo, NeverRefreshedSaysNever )
{
	EXPECT_EQ( "Last refreshed: never", LastRefreshedLine( false, 0 ));
	// The age is ignored entirely when nothing has happened, so a stale counter cannot leak in.
	EXPECT_EQ( "Last refreshed: never", LastRefreshedLine( false, 99999 ));
}

TEST( TimeAgo, TheLineCarriesTheAge )
{
	EXPECT_EQ( "Last refreshed: 5 mins ago", LastRefreshedLine( true, 300 ));
	EXPECT_EQ( "Last refreshed: just now", LastRefreshedLine( true, 0 ));
}
