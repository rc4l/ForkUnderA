// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/hostlist_compute.h"

using zx::BuildHostListRows;
using zx::FindHostListRow;
using zx::HostListRow;

namespace
{

// The shipped catalogue: two packs that play one way, and Skulltag with its six.
std::vector<int> Shipped( )
{
	std::vector<int> counts;
	counts.push_back( 0 );	// Classic Duel
	counts.push_back( 0 );	// The Eon Collection
	counts.push_back( 6 );	// Skulltag
	return counts;
}

} // namespace

TEST( HostList, ShutTheListIsOneRowPerExperience )
{
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), -1 );

	ASSERT_EQ( 3u, rows.size( ));
	for ( size_t i = 0; i < rows.size( ); ++i )
	{
		EXPECT_EQ( static_cast<int>( i ), rows[i].entry ) << "row " << i;
		EXPECT_EQ( -1, rows[i].variant ) << "row " << i;
	}
}

TEST( HostList, OpeningAnEntryHangsItsWaysOfPlayingUnderIt )
{
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), 2 );

	ASSERT_EQ( 9u, rows.size( ));

	// The two above it are untouched, which is the point of opening in place rather than replacing
	// the list: the player can still see what else there is.
	EXPECT_EQ( 0, rows[0].entry );
	EXPECT_EQ( -1, rows[0].variant );
	EXPECT_EQ( 1, rows[1].entry );

	EXPECT_EQ( 2, rows[2].entry );
	EXPECT_EQ( -1, rows[2].variant ) << "the entry's own row comes first";

	for ( int v = 0; v < 6; ++v )
	{
		EXPECT_EQ( 2, rows[3 + v].entry ) << "variant " << v;
		EXPECT_EQ( v, rows[3 + v].variant ) << "variant " << v;
	}
}

TEST( HostList, OnlyOneEntryIsEverOpen )
{
	// Swept, because the list is walked by index everywhere and an extra open entry would shift every
	// row below it without anything noticing.
	const std::vector<int> counts = Shipped( );

	for ( int open = -1; open < static_cast<int>( counts.size( )); ++open )
	{
		const std::vector<HostListRow> rows = BuildHostListRows( counts, open );
		const size_t extra = ( open >= 0 ) ? static_cast<size_t>( counts[open] ) : 0;

		EXPECT_EQ( counts.size( ) + extra, rows.size( )) << "open " << open;
	}
}

TEST( HostList, AnEntryWithNothingToShowCannotBeOpened )
{
	// A caret on a row that cannot open is a promise the list does not keep, so asking for it does
	// nothing rather than producing a row that is about nothing.
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), 0 );

	EXPECT_EQ( 3u, rows.size( ));
}

TEST( HostList, AnEmptyCatalogueHasNoRows )
{
	EXPECT_TRUE( BuildHostListRows( std::vector<int>( ), -1 ).empty( ));
	EXPECT_TRUE( BuildHostListRows( std::vector<int>( ), 0 ).empty( ));
}

TEST( HostList, AnOpenEntryThatIsNotThereIsIgnored )
{
	// The catalogue can be re-read while something is open.
	EXPECT_EQ( 3u, BuildHostListRows( Shipped( ), 99 ).size( ));
	EXPECT_EQ( 3u, BuildHostListRows( Shipped( ), -7 ).size( ));
}

// ------------------------------------------------------------ finding the cursor

TEST( HostList, EveryRowIsFoundByWhatItIs )
{
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), 2 );

	for ( size_t i = 0; i < rows.size( ); ++i )
		EXPECT_EQ( static_cast<int>( i ), FindHostListRow( rows, rows[i].entry, rows[i].variant ));
}

TEST( HostList, AChosenVariantWhoseEntryIsShutFallsBackToTheEntryRow )
{
	// THE case that would otherwise leave the cursor nowhere: a way of playing is chosen, then the
	// entry is closed. The choice still stands, so the row that represents it has to be the entry's.
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), -1 );

	EXPECT_EQ( 2, FindHostListRow( rows, 2, 4 ));
	EXPECT_EQ( 2, FindHostListRow( rows, 2, -1 ));
}

TEST( HostList, ASelectionTheListDoesNotContainAnswersNowhere )
{
	// Better than a row index that happens to be in range: the caller can tell that its selection is
	// stale, rather than quietly highlighting somebody else's experience.
	const std::vector<HostListRow> rows = BuildHostListRows( Shipped( ), -1 );

	EXPECT_EQ( -1, FindHostListRow( rows, 9, -1 ));
	EXPECT_EQ( -1, FindHostListRow( rows, -1, -1 ));
	EXPECT_EQ( -1, FindHostListRow( std::vector<HostListRow>( ), 0, -1 ));
}
