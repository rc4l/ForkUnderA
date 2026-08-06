// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/stableline_compute.h"

using zx::MaskVarying;
using std::string;

TEST( MaskVarying, ReplacesEveryDigit )
{
	EXPECT_EQ( "00%", MaskVarying( "17%", '0' ));
	EXPECT_EQ( "0000000000", MaskVarying( "0123456789", '0' ));
}

TEST( MaskVarying, ReplacesEverySpaceToo )
{
	// The padding that fixes the character count is made of spaces, and a space is narrower than a
	// digit -- so masking only digits leaves " 5%" and "99%" different widths, which is the whole bug.
	EXPECT_EQ( "00%", MaskVarying( " 5%", '0' ));
	EXPECT_EQ( "00%", MaskVarying( "99%", '0' ));
}

TEST( MaskVarying, LeavesLettersAndPunctuationAlone )
{
	// Those do not change during a transfer, so they are already stable. Masking them would make the
	// box wider than the line for no reason at all.
	EXPECT_EQ( "wad.0000MB", MaskVarying( "wad. 17 MB", '0' ));

	// A period and a percent sign survive; every digit and space around them does not.
	EXPECT_EQ( "a00.0%b", MaskVarying( "a 1.2%b", '0' ));
}

TEST( MaskVarying, GivesTheSameAnswerForEveryValueOfTheSameShape )
{
	// The whole point: two frames of the same transfer must mask to an identical string, so the box
	// measured from it is identical too. Note the padding -- " 5" against "99", "  1.1" against
	// "890.4" -- which is exactly what masking spaces as well as digits is for.
	const string a = "ghostpack900.wad   5%  (  1.1 MB of 900.0 MB)";
	const string b = "ghostpack900.wad  99%  (890.4 MB of 900.0 MB)";

	ASSERT_EQ( a.size( ), b.size( ));		// the caller pads to make this true
	EXPECT_EQ( MaskVarying( a, '0' ), MaskVarying( b, '0' ));
}

TEST( MaskVarying, IsStableAcrossAWholeTransfer )
{
	// Every percentage from 0 to 100 against a fixed total, laid out the way StatusLine lays it out.
	// One masked string for all of them, or the panel moves at some point during the download.
	string first;
	for ( int pct = 0; pct <= 100; ++pct )
	{
		char line[128];
		snprintf( line, sizeof( line ), "ghostpack900.wad  %3d%%  (%5.1f MB of 900.0 MB)",
			pct, 900.0 * pct / 100.0 );

		const string masked = MaskVarying( line, '0' );
		if ( pct == 0 )
			first = masked;
		else
			EXPECT_EQ( first, masked ) << "moved at " << pct << "%";
	}
}

TEST( MaskVarying, WorksWithAnyChosenDigit )
{
	// Which digit is widest is a property of the font, so the caller picks it.
	EXPECT_EQ( "88%", MaskVarying( "17%", '8' ));
	EXPECT_EQ( "444MB", MaskVarying( "17 MB", '4' ));
}

TEST( MaskVarying, HandlesStringsWithNothingToMask )
{
	EXPECT_EQ( "", MaskVarying( "", '0' ));
	EXPECT_EQ( "ready", MaskVarying( "ready", '0' ));
}

TEST( MaskVarying, NeverChangesTheLength )
{
	// A mask that grew or shrank would make the box the wrong size, which is the bug it exists to
	// prevent rather than a variation on it.
	const string samples[] = {
		"", "0", "abc", "1234567890", "a1b2c3", "  9%  ( 0.0 MB of 900.0 MB)",
	};

	for ( size_t i = 0; i < sizeof( samples ) / sizeof( samples[0] ); ++i )
		EXPECT_EQ( samples[i].size( ), MaskVarying( samples[i], '0' ).size( )) << samples[i];
}
