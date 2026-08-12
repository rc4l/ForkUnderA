// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/hostlist_compute.h"

using zx::BuildHostListRows;
using zx::FindHostListRow;
using zx::HostListRow;

namespace
{

// The shipped catalogue: a pack that plays one way, and two with several.
std::vector<int> Shipped( )
{
	std::vector<int> counts;
	counts.push_back( 0 );	// Classic Duel
	counts.push_back( 4 );	// The Eon Collection
	counts.push_back( 8 );	// Skulltag
	return counts;
}

std::vector<bool> Shut( )
{
	return std::vector<bool>( 3, false );
}

std::vector<bool> OpenOnly( size_t which )
{
	std::vector<bool> open( 3, false );
	open[which] = true;
	return open;
}

} // namespace

TEST( HostList, ShutTheListIsOneRowPerExperience )
{
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), Shut( ));

	ASSERT_EQ( 3u, rows.size( ));
	for ( size_t i = 0; i < rows.size( ); ++i )
	{
		EXPECT_EQ( static_cast<int>( i ), rows[i].entry ) << "row " << i;
		EXPECT_EQ( -1, rows[i].variant ) << "row " << i;
	}
}

TEST( HostList, OpeningAnEntryHangsItsWaysOfPlayingUnderIt )
{
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), OpenOnly( 2 ));

	ASSERT_EQ( 11u, rows.size( ));

	// The ones above it are untouched, which is the point of opening in place rather than replacing
	// the list: the player can still see what else there is.
	EXPECT_EQ( 0, rows[0].entry );
	EXPECT_EQ( -1, rows[0].variant );
	EXPECT_EQ( 1, rows[1].entry );
	EXPECT_EQ( -1, rows[1].variant );

	EXPECT_EQ( 2, rows[2].entry );
	EXPECT_EQ( -1, rows[2].variant ) << "the entry's own row comes first";

	for ( int v = 0; v < 8; ++v )
	{
		EXPECT_EQ( 2, rows[3 + v].entry ) << "variant " << v;
		EXPECT_EQ( v, rows[3 + v].variant ) << "variant " << v;
	}
}

TEST( HostList, SeveralMayBeOpenAtOnce )
{
	// THE point of taking a vector rather than one index. One at a time meant opening the second
	// pack shut the first, so the thing you were comparing against vanished as you went to look.
	std::vector<bool> open( 3, false );
	open[1] = true;
	open[2] = true;

	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), open );

	ASSERT_EQ( 3u + 4u + 8u, rows.size( ));

	EXPECT_EQ( 1, rows[1].entry );
	EXPECT_EQ( -1, rows[1].variant );
	EXPECT_EQ( 0, rows[2].variant ) << "Eon's ways follow Eon";

	EXPECT_EQ( 2, rows[6].entry );
	EXPECT_EQ( -1, rows[6].variant ) << "Skulltag's own row comes after Eon's four";
	EXPECT_EQ( 0, rows[7].variant );
}

TEST( HostList, EveryCombinationOfOpenAddsUp )
{
	// Swept over all eight combinations, because the list is walked by index everywhere and one
	// extra or missing row shifts everything below it without anything noticing.
	const std::vector<int> counts = Shipped( );

	for ( int bits = 0; bits < 8; ++bits )
	{
		std::vector<bool> open( 3, false );
		size_t expected = counts.size( );

		for ( size_t i = 0; i < 3; ++i )
		{
			if (( bits & ( 1 << i )) == 0 )
				continue;

			open[i] = true;
			expected += static_cast<size_t>( counts[i] );
		}

		EXPECT_EQ( expected, BuildHostListRows( counts, open ).size( )) << "bits " << bits;
	}
}

TEST( HostList, AnEntryWithNothingToShowCannotBeOpened )
{
	// A caret on a row that cannot open is a promise the list does not keep, so asking for it does
	// nothing rather than producing a row that is about nothing.
	EXPECT_EQ( 3u, BuildHostListRows( Shipped( ), OpenOnly( 0 )).size( ));
}

TEST( HostList, AnEmptyCatalogueHasNoRows )
{
	EXPECT_TRUE( BuildHostListRows( std::vector<int>( ), std::vector<bool>( ) ).empty( ));
	EXPECT_TRUE( BuildHostListRows( std::vector<int>( ), std::vector<bool>( 3, true )).empty( ));
}

TEST( HostList, ACallerWithNoStateYetPassesNothingAndGetsAShutList )
{
	// Anything past the end of `open` is shut, so the first frame does not have to build a vector of
	// falses to say "I have not opened anything".
	EXPECT_EQ( 3u, BuildHostListRows( Shipped( ), std::vector<bool>( )).size( ));
	EXPECT_EQ( 3u, BuildHostListRows( Shipped( ), std::vector<bool>( 1, false )).size( ));
}

// ------------------------------------------------------------ finding the cursor

TEST( HostList, EveryRowIsFoundByWhatItIs )
{
	std::vector<bool> open( 3, false );
	open[1] = true;
	open[2] = true;

	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), open );

	for ( size_t i = 0; i < rows.size( ); ++i )
		EXPECT_EQ( static_cast<int>( i ), FindHostListRow( rows, rows[i].entry, rows[i].variant ));
}

TEST( HostList, AChosenVariantWhoseEntryIsShutFallsBackToTheEntryRow )
{
	// THE case that would otherwise leave the cursor nowhere: a way of playing is chosen, then the
	// entry is closed. The choice still stands, so the row that represents it has to be the entry's.
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), Shut( ));

	EXPECT_EQ( 2, FindHostListRow( rows, 2, 4 ));
	EXPECT_EQ( 2, FindHostListRow( rows, 2, -1 ));
}

TEST( HostList, ASelectionTheListDoesNotContainAnswersNowhere )
{
	// Better than a row index that happens to be in range: the caller can tell that its selection is
	// stale, rather than quietly highlighting somebody else's experience.
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), Shut( ));

	EXPECT_EQ( -1, FindHostListRow( rows, 9, -1 ));
	EXPECT_EQ( -1, FindHostListRow( rows, -1, -1 ));
	EXPECT_EQ( -1, FindHostListRow( std::vector<HostListRow>( ), 0, -1 ));
}

TEST(HostListRows, AFreshRowIsTheFirstEntrysOwnRow)
{
	// [rc4l] -1 for the variant is what says "the entry itself" rather than one of its ways of
	// playing, and it has to be the default: zero would be a real variant index and would make a
	// default-constructed row point at somebody's first variant.
	const HostListRow row;

	EXPECT_EQ(0, row.entry);
	EXPECT_EQ(-1, row.variant);
}
