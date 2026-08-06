// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/bytesize_compute.h"

#include <cctype>
#include <cstdlib>

using zx::FormatByteSize;

namespace
{
const unsigned long long KB = 1024ULL;
const unsigned long long MB = 1024ULL * 1024ULL;
const unsigned long long GB = 1024ULL * 1024ULL * 1024ULL;
const unsigned long long TB = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
} // namespace

TEST( FormatByteSize, UsesBytesBelowAKilobyte )
{
	EXPECT_EQ( "0b", FormatByteSize( 0 ));
	EXPECT_EQ( "1b", FormatByteSize( 1 ));
	EXPECT_EQ( "900b", FormatByteSize( 900 ));
	EXPECT_EQ( "1023b", FormatByteSize( 1023 ));
}

TEST( FormatByteSize, StepsUpAtEachPowerOf1024 )
{
	EXPECT_EQ( "1kb", FormatByteSize( KB ));
	EXPECT_EQ( "1mb", FormatByteSize( MB ));
	EXPECT_EQ( "1gb", FormatByteSize( GB ));
	EXPECT_EQ( "1tb", FormatByteSize( TB ));
}

TEST( FormatByteSize, PrintsTheSortOfNumberAWadActuallyIs )
{
	EXPECT_EQ( "23mb", FormatByteSize( 23 * MB ));
	EXPECT_EQ( "14mb", FormatByteSize( 14263296 ));		// doom2.wad
	EXPECT_EQ( "512kb", FormatByteSize( 512 * KB ));
}

TEST( FormatByteSize, RoundsToNearestRatherThanTruncating )
{
	// Truncation would call a 1.9 mb file 1mb, which is the kind of wrong that makes a download look
	// half the size it is.
	EXPECT_EQ( "2mb", FormatByteSize( MB + ( MB * 9 / 10 )));
	EXPECT_EQ( "1mb", FormatByteSize( MB + ( MB / 10 )));
	EXPECT_EQ( "2kb", FormatByteSize( 1536 ));			// exactly 1.5 kb, rounds away from zero
	EXPECT_EQ( "1kb", FormatByteSize( 1535 ));
}

TEST( FormatByteSize, CarriesIntoTheNextUnitWhenRoundingFillsThisOne )
{
	// 1023.99 kb. "1024kb" defeats the point of having a unit at all.
	EXPECT_EQ( "1mb", FormatByteSize( MB - 1 ));
	EXPECT_EQ( "1gb", FormatByteSize( GB - 1 ));

	// But a byte under a kilobyte is still 1023 BYTES -- there is no smaller unit for it to have been
	// measured in, so nothing carries.
	EXPECT_EQ( "1023b", FormatByteSize( KB - 1 ));
}

TEST( FormatByteSize, StopsAtTerabytesRatherThanInventingAUnit )
{
	// The largest unit is the last one; past it the number simply grows.
	EXPECT_EQ( "1024tb", FormatByteSize( 1024ULL * TB ));
}

TEST( FormatByteSize, SurvivesTheTopOfTheRange )
{
	// Adding half a unit to round would wrap here. Nothing this size is a file, but a formatter that
	// answers "0b" for the largest number there is would be actively misleading.
	const std::string out = FormatByteSize( static_cast<unsigned long long>( -1 ));
	EXPECT_NE( "0b", out );
	EXPECT_EQ( "tb", out.substr( out.size( ) - 2 ));
}

TEST( FormatByteSize, NeverSpendsMoreThanTwoCharactersOnTheUnit )
{
	// The constraint the whole unit exists for: this sits beside a filename in a list that is already
	// tight, so anything the unit costs comes out of the name.
	const unsigned long long samples[] = {
		0, 1, 999, 1023, KB, 1536, 100 * KB, MB - 1, MB, 23 * MB, 999 * MB,
		GB, 5 * GB, TB, 1024ULL * TB, static_cast<unsigned long long>( -1 ),
	};

	for ( size_t i = 0; i < sizeof( samples ) / sizeof( samples[0] ); ++i )
	{
		const std::string out = FormatByteSize( samples[i] );

		size_t digits = 0;
		while (( digits < out.size( )) && ( isdigit( static_cast<unsigned char>( out[digits] )) != 0 ))
			++digits;

		EXPECT_GT( digits, 0u ) << out;						// always leads with a number
		EXPECT_LE( out.size( ) - digits, 2u ) << out;		// and never more than two letters after it
		EXPECT_GE( out.size( ) - digits, 1u ) << out;		// and never no unit at all
	}
}

TEST( FormatByteSize, NeverGoesBackwards )
{
	// A sweep across every boundary: a bigger file must never print a smaller-looking size. This is
	// what would break if the carry above promoted a unit without dividing the number with it.
	const unsigned long long points[] = { 0, KB, MB, GB, TB };
	unsigned long long previousBytes = 0;
	std::string previous = FormatByteSize( 0 );

	for ( size_t p = 0; p < 5; ++p )
	{
		for ( long long d = -3; d <= 3; ++d )
		{
			const long long at = static_cast<long long>( points[p] ) + d;
			if ( at < 0 )
				continue;

			const unsigned long long bytes = static_cast<unsigned long long>( at );
			const std::string out = FormatByteSize( bytes );

			// Same unit -> the number must not shrink. Different unit -> it must have gone up, which
			// the ordering of the sweep guarantees.
			if ( out.substr( out.find_first_not_of( "0123456789" )) ==
				previous.substr( previous.find_first_not_of( "0123456789" )))
			{
				EXPECT_LE( atoll( previous.c_str( )), atoll( out.c_str( )))
					<< previousBytes << " -> " << bytes;
			}

			previousBytes = bytes;
			previous = out;
		}
	}
}
