// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/continue/computation/continuehistory_compute.h"

#include <cstdio>

using namespace zx;

namespace
{

ContinueRecord Server( const char *address, int stamp )
{
	ContinueRecord r;
	r.kind = ContinueKind::Server;
	r.address = address;
	r.stamp = stamp;
	return r;
}

ContinueRecord Single( const char *map, const char *wad, int stamp )
{
	ContinueRecord r;
	r.kind = ContinueKind::Single;
	r.savePath = "/tmp/continue/offline-1.zds";
	r.saveVersion = 4552;
	r.mapName = map;
	r.mapWad = wad;
	r.iwad = "doom2.wad";
	r.stamp = stamp;

	ContinueRecord::Wad w;
	w.name = wad;
	w.hash = "0123456789abcdef0123456789abcdef";
	r.wads.push_back( w );
	return r;
}

ContinueRecord Hosted( const char *map, int stamp )
{
	ContinueRecord r;
	r.kind = ContinueKind::Hosted;
	r.host.map = map;
	r.host.iwad = "doom2.wad";
	r.host.gameMode = 3;
	r.stamp = stamp;
	return r;
}

std::vector<ContinueRecord> Nothing()
{
	return std::vector<ContinueRecord>();
}

} // namespace

// ---------------------------------------------------------------- the size limit

TEST( ContinueHistory, TheLimitIsPulledIntoItsRange )
{
	EXPECT_EQ( kContinueHistoryMin, ClampContinueHistoryLimit( 0 ));
	EXPECT_EQ( kContinueHistoryMin, ClampContinueHistoryLimit( -12 ));
	EXPECT_EQ( kContinueHistoryMax, ClampContinueHistoryLimit( 500 ));
	EXPECT_EQ( 10, ClampContinueHistoryLimit( 10 ));
	EXPECT_EQ( kContinueHistoryMin, ClampContinueHistoryLimit( kContinueHistoryMin ));
	EXPECT_EQ( kContinueHistoryMax, ClampContinueHistoryLimit( kContinueHistoryMax ));
}

TEST( ContinueHistory, ZeroIsNotAnAllowedSize )
{
	// Zero entries is the feature switched off, not a shorter list, and a size control must not be
	// an off switch at one end of its travel -- see the header.
	EXPECT_GE( ClampContinueHistoryLimit( 0 ), 1 );
}

// ---------------------------------------------------------------- identity

TEST( ContinueHistory, AServerIsItsAddress )
{
	ContinueRecord a = Server( "10.0.0.5:10666", 1 );
	ContinueRecord b = Server( "10.0.0.5:10666", 9 );
	b.serverName = "renamed since";

	EXPECT_EQ( ContinueIdentity( a ), ContinueIdentity( b ));
	EXPECT_NE( ContinueIdentity( a ), ContinueIdentity( Server( "10.0.0.6:10666", 1 )));
}

TEST( ContinueHistory, ALocalGameIsItsMapAndItsFiles )
{
	// MAP01 of one megawad is not MAP01 of another, and a history that thought otherwise would
	// overwrite one with the other.
	EXPECT_NE( ContinueIdentity( Single( "MAP01", "sunder.wad", 1 )),
		ContinueIdentity( Single( "MAP01", "valiant.wad", 1 )));

	EXPECT_NE( ContinueIdentity( Single( "MAP01", "sunder.wad", 1 )),
		ContinueIdentity( Single( "MAP02", "sunder.wad", 1 )));

	EXPECT_EQ( ContinueIdentity( Single( "MAP01", "sunder.wad", 1 )),
		ContinueIdentity( Single( "MAP01", "sunder.wad", 44 )));
}

TEST( ContinueHistory, SpellingDoesNotMakeASecondEntry )
{
	// The same address or file written in a different case is the same thing, and two rows saying it
	// would be two of the ten spent on one session.
	EXPECT_EQ( ContinueIdentity( Server( "10.0.0.5:10666", 1 )),
		ContinueIdentity( Server( "10.0.0.5:10666", 1 )));

	ContinueRecord upper = Single( "MAP01", "Sunder.WAD", 1 );
	ContinueRecord lower = Single( "map01", "sunder.wad", 1 );
	EXPECT_EQ( ContinueIdentity( upper ), ContinueIdentity( lower ));
}

TEST( ContinueHistory, AHostedGameIsWhatWouldStartItAgain )
{
	EXPECT_EQ( ContinueIdentity( Hosted( "MAP07", 1 )), ContinueIdentity( Hosted( "MAP07", 5 )));
	EXPECT_NE( ContinueIdentity( Hosted( "MAP07", 1 )), ContinueIdentity( Hosted( "MAP08", 1 )));

	// The mode is part of it: the same map as a deathmatch is not the same match as a coop.
	ContinueRecord coop = Hosted( "MAP07", 1 );
	coop.host.gameMode = 0;
	EXPECT_NE( ContinueIdentity( Hosted( "MAP07", 1 )), ContinueIdentity( coop ));

	// And so are the files, for the same reason a local game's are.
	ContinueRecord modded = Hosted( "MAP07", 1 );
	modded.host.pwads.push_back( "brutal.pk3" );
	EXPECT_NE( ContinueIdentity( Hosted( "MAP07", 1 )), ContinueIdentity( modded ));
}

TEST( ContinueHistory, NothingToContinueHasNoIdentity )
{
	EXPECT_TRUE( ContinueIdentity( ContinueRecord() ).empty() );
}

TEST( ContinueHistory, DifferentKindsNeverCollide )
{
	// Three namespaces in one string, so a hosted MAP07 and an offline MAP07 cannot replace one
	// another however similarly they are named.
	ContinueRecord hostedish = Single( "MAP07", "doom2.wad", 1 );

	EXPECT_NE( ContinueIdentity( Hosted( "MAP07", 1 )), ContinueIdentity( hostedish ));
	EXPECT_NE( ContinueIdentity( Server( "MAP07", 1 )), ContinueIdentity( hostedish ));
}

// ---------------------------------------------------------------- the activity column

TEST( ContinueHistory, AServerIsNamedIfWeKnowItsName )
{
	ContinueRecord r = Server( "10.0.0.5:10666", 1 );
	EXPECT_EQ( "10.0.0.5:10666", ContinueEntryLabel( r ));

	r.serverName = "Best Ever GvH";
	EXPECT_EQ( "Best Ever GvH", ContinueEntryLabel( r ));
}

TEST( ContinueHistory, ALocalGameNamesTheMegawadItWasIn )
{
	EXPECT_EQ( "MAP01 in sunder.wad", ContinueEntryLabel( Single( "MAP01", "sunder.wad", 1 )));

	// Nothing to attribute it to: the map alone, rather than a dangling "in".
	ContinueRecord bare = Single( "MAP01", "", 1 );
	bare.mapWad.clear();
	EXPECT_EQ( "MAP01", ContinueEntryLabel( bare ));
}

TEST( ContinueHistory, AHostedGameSaysSo )
{
	EXPECT_EQ( "Hosting MAP07", ContinueEntryLabel( Hosted( "MAP07", 1 )));
}

TEST( ContinueHistory, NothingToContinueHasNoLabel )
{
	EXPECT_TRUE( ContinueEntryLabel( ContinueRecord() ).empty() );
}

// ---------------------------------------------------------------- the last played column

TEST( ContinueHistory, AnEntryWithNoClockShowsADash )
{
	// Written by a build from before the field existed. It is not from 1970 and must not say so.
	EXPECT_EQ( "-", FormatLastPlayed( 1000000, 0 ));
	EXPECT_EQ( "-", FormatLastPlayed( 1000000, -5 ));
}

TEST( ContinueHistory, RecentIsJustNow )
{
	EXPECT_EQ( "just now", FormatLastPlayed( 1000, 1000 ));
	EXPECT_EQ( "just now", FormatLastPlayed( 1059, 1000 ));
}

TEST( ContinueHistory, AClockThatWentBackwardsStillReads )
{
	// What a machine that corrected its time leaves behind. The row is still the thing they last
	// played; only its age is unsayable, so it must not print a negative number of minutes.
	EXPECT_EQ( "just now", FormatLastPlayed( 1000, 900000 ));
}

TEST( ContinueHistory, TheUnitGrowsWithTheGap )
{
	const long long now = 1000000000LL;

	EXPECT_EQ( "1 min ago",   FormatLastPlayed( now, now - 60 ));
	EXPECT_EQ( "59 mins ago", FormatLastPlayed( now, now - 59 * 60 ));
	EXPECT_EQ( "1 hour ago",  FormatLastPlayed( now, now - 60 * 60 ));
	EXPECT_EQ( "23 hours ago", FormatLastPlayed( now, now - 23 * 60 * 60 ));
	EXPECT_EQ( "1 day ago",   FormatLastPlayed( now, now - 24 * 60 * 60 ));
	EXPECT_EQ( "6 days ago",  FormatLastPlayed( now, now - 6 * 24 * 60 * 60 ));
	EXPECT_EQ( "1 week ago",  FormatLastPlayed( now, now - 7 * 24 * 60 * 60 ));
	EXPECT_EQ( "2 weeks ago", FormatLastPlayed( now, now - 14 * 24 * 60 * 60 ));
	EXPECT_EQ( "1 year ago",  FormatLastPlayed( now, now - 365LL * 24 * 60 * 60 ));
	EXPECT_EQ( "3 years ago", FormatLastPlayed( now, now - 3 * 365LL * 24 * 60 * 60 ));
}

// ---------------------------------------------------------------- ordering and capping

TEST( ContinueHistory, TrimPutsTheNewestFirst )
{
	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 3 ));
	history.push_back( Server( "b:1", 9 ));
	history.push_back( Server( "c:1", 5 ));

	const std::vector<ContinueRecord> out = TrimContinueHistory( history, 10 );

	ASSERT_EQ( 3u, out.size() );
	EXPECT_EQ( "b:1", out[0].address );
	EXPECT_EQ( "c:1", out[1].address );
	EXPECT_EQ( "a:1", out[2].address );
}

TEST( ContinueHistory, TrimDropsTheOldestPastTheLimit )
{
	std::vector<ContinueRecord> history;
	for ( int i = 1; i <= 20; ++i )
	{
		char address[32];
		snprintf( address, sizeof address, "10.0.0.%d:10666", i );
		history.push_back( Server( address, i ));
	}

	const std::vector<ContinueRecord> out = TrimContinueHistory( history, 5 );

	ASSERT_EQ( 5u, out.size() );
	EXPECT_EQ( 20, out[0].stamp );
	EXPECT_EQ( 16, out[4].stamp );
}

TEST( ContinueHistory, TrimObeysTheSameRangeAsTheOption )
{
	std::vector<ContinueRecord> history;
	for ( int i = 1; i <= 60; ++i )
		history.push_back( Server( "a:1", i ));

	EXPECT_EQ( 1u, TrimContinueHistory( history, 0 ).size() );
	EXPECT_EQ( static_cast<size_t>( kContinueHistoryMax ),
		TrimContinueHistory( history, 999 ).size() );
}

TEST( ContinueHistory, TrimKeepsTheFileOrderWhenStampsTie )
{
	// A list that shuffles between two reads of the same file is a list nobody can point at.
	std::vector<ContinueRecord> history;
	history.push_back( Server( "first:1", 4 ));
	history.push_back( Server( "second:1", 4 ));
	history.push_back( Server( "third:1", 4 ));

	const std::vector<ContinueRecord> out = TrimContinueHistory( history, 10 );

	ASSERT_EQ( 3u, out.size() );
	EXPECT_EQ( "first:1", out[0].address );
	EXPECT_EQ( "second:1", out[1].address );
	EXPECT_EQ( "third:1", out[2].address );
}

TEST( ContinueHistory, TrimmingNothingIsNothing )
{
	EXPECT_TRUE( TrimContinueHistory( Nothing(), 10 ).empty() );
}

TEST( ContinueHistory, TheNextStampBeatsEverythingInTheList )
{
	EXPECT_EQ( 1, NextContinueStamp( Nothing() ));

	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 3 ));
	history.push_back( Server( "b:1", 41 ));
	history.push_back( Server( "c:1", 12 ));

	EXPECT_EQ( 42, NextContinueStamp( history ));
}

// ---------------------------------------------------------------- inserting

TEST( ContinueHistory, PlayingSomethingNewAddsARow )
{
	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 1 ));

	const std::vector<ContinueRecord> out =
		InsertContinueEntry( history, Single( "MAP01", "sunder.wad", 0 ), 10 );

	ASSERT_EQ( 2u, out.size() );
	EXPECT_EQ( ContinueKind::Single, out[0].kind );
	EXPECT_EQ( ContinueKind::Server, out[1].kind );
}

TEST( ContinueHistory, PlayingTheSameThingAgainMovesItRatherThanRepeatingIt )
{
	// Three evenings on the same server is one thing done three times. A history that showed it
	// three times would have spent three of its rows saying the same sentence.
	std::vector<ContinueRecord> history;
	history.push_back( Server( "evening:1", 1 ));
	history.push_back( Single( "MAP01", "sunder.wad", 2 ));

	ContinueRecord again = Server( "evening:1", 0 );
	again.serverName = "learned its name since";

	const std::vector<ContinueRecord> out = InsertContinueEntry( history, again, 10 );

	ASSERT_EQ( 2u, out.size() );
	EXPECT_EQ( "evening:1", out[0].address );
	EXPECT_EQ( "learned its name since", out[0].serverName );	// and the row is rewritten, not kept
	EXPECT_EQ( ContinueKind::Single, out[1].kind );
}

TEST( ContinueHistory, AnInsertedEntryOutranksEverythingAlreadyThere )
{
	// The stamp the caller brought may be older than the list it is joining -- it came from a file
	// read that another copy of the engine has written since.
	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 90 ));

	const std::vector<ContinueRecord> out = InsertContinueEntry( history, Server( "b:1", 2 ), 10 );

	ASSERT_EQ( 2u, out.size() );
	EXPECT_EQ( "b:1", out[0].address );
	EXPECT_GT( out[0].stamp, 90 );
}

TEST( ContinueHistory, AnAlreadyNewerStampIsLeftAlone )
{
	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 5 ));

	const std::vector<ContinueRecord> out = InsertContinueEntry( history, Server( "b:1", 77 ), 10 );

	ASSERT_EQ( 2u, out.size() );
	EXPECT_EQ( 77, out[0].stamp );
}

TEST( ContinueHistory, InsertingObeysTheLimit )
{
	std::vector<ContinueRecord> history;
	for ( int i = 1; i <= 10; ++i )
	{
		char address[32];
		snprintf( address, sizeof address, "10.0.0.%d:10666", i );
		history.push_back( Server( address, i ));
	}

	const std::vector<ContinueRecord> out = InsertContinueEntry( history, Server( "new:1", 0 ), 10 );

	ASSERT_EQ( 10u, out.size() );
	EXPECT_EQ( "new:1", out[0].address );
	EXPECT_EQ( "10.0.0.2:10666", out[9].address );	// the oldest fell off, not the newest
}

TEST( ContinueHistory, NothingToContinueIsNotRemembered )
{
	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 1 ));

	const std::vector<ContinueRecord> out = InsertContinueEntry( history, ContinueRecord(), 10 );

	ASSERT_EQ( 1u, out.size() );
	EXPECT_EQ( "a:1", out[0].address );
}

TEST( ContinueHistory, TheFirstThingEverPlayedStartsTheList )
{
	const std::vector<ContinueRecord> out =
		InsertContinueEntry( Nothing(), Server( "a:1", 0 ), 10 );

	ASSERT_EQ( 1u, out.size() );
	EXPECT_EQ( 1, out[0].stamp );
}

// ---------------------------------------------------------------- finding and removing

TEST( ContinueHistory, AnEntryCanBeFoundByWhatItIs )
{
	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 1 ));
	history.push_back( Single( "MAP01", "sunder.wad", 2 ));

	const ContinueRecord *found =
		FindContinueEntry( history, ContinueIdentity( Single( "MAP01", "sunder.wad", 99 )));

	ASSERT_TRUE( found != NULL );
	EXPECT_EQ( 2, found->stamp );

	EXPECT_TRUE( FindContinueEntry( history, ContinueIdentity( Server( "gone:1", 1 ))) == NULL );
	EXPECT_TRUE( FindContinueEntry( history, "" ) == NULL );
}

TEST( ContinueHistory, ARowCanBeTakenOut )
{
	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 3 ));
	history.push_back( Server( "b:1", 2 ));
	history.push_back( Server( "c:1", 1 ));

	const std::vector<ContinueRecord> out = RemoveContinueEntry( history, 1 );

	ASSERT_EQ( 2u, out.size() );
	EXPECT_EQ( "a:1", out[0].address );
	EXPECT_EQ( "c:1", out[1].address );
}

TEST( ContinueHistory, RemovingARowThatIsNotThereChangesNothing )
{
	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 1 ));

	EXPECT_EQ( 1u, RemoveContinueEntry( history, -1 ).size() );
	EXPECT_EQ( 1u, RemoveContinueEntry( history, 1 ).size() );
	EXPECT_EQ( 1u, RemoveContinueEntry( history, 900 ).size() );
	EXPECT_TRUE( RemoveContinueEntry( Nothing(), 0 ).empty() );
}

// ---------------------------------------------------------------- the file

TEST( ContinueHistory, AListSurvivesTheRoundTrip )
{
	std::vector<ContinueRecord> history;
	history.push_back( Server( "10.0.0.5:10666", 9 ));
	history[0].serverName = "Best Ever GvH";
	history[0].password = "a password with spaces";
	history[0].playedAt = 1788000000LL;
	history.push_back( Single( "MAP12", "sunder.wad", 8 ));
	history.push_back( Hosted( "MAP07", 7 ));

	std::vector<ContinueRecord> back;
	ASSERT_TRUE( ParseContinueHistory( SerialiseContinueHistory( history ), back ));

	ASSERT_EQ( 3u, back.size() );

	EXPECT_EQ( ContinueKind::Server, back[0].kind );
	EXPECT_EQ( "10.0.0.5:10666", back[0].address );
	EXPECT_EQ( "Best Ever GvH", back[0].serverName );
	EXPECT_EQ( "a password with spaces", back[0].password );
	EXPECT_EQ( 1788000000LL, back[0].playedAt );
	EXPECT_EQ( 9, back[0].stamp );

	EXPECT_EQ( ContinueKind::Single, back[1].kind );
	EXPECT_EQ( "MAP12", back[1].mapName );
	EXPECT_EQ( "sunder.wad", back[1].mapWad );
	ASSERT_EQ( 1u, back[1].wads.size() );
	EXPECT_EQ( "sunder.wad", back[1].wads[0].name );

	EXPECT_EQ( ContinueKind::Hosted, back[2].kind );
	EXPECT_EQ( "MAP07", back[2].host.map );
	EXPECT_EQ( 3, back[2].host.gameMode );
}

TEST( ContinueHistory, AnEmptyListIsStillAFile )
{
	// "Nothing to continue" has to be something the file can SAY. Without it, load falls back to
	// migrating the old records and a player who cleared their history finds it back next launch.
	const std::string text = SerialiseContinueHistory( Nothing() );
	EXPECT_FALSE( text.empty() );

	std::vector<ContinueRecord> back;
	EXPECT_TRUE( ParseContinueHistory( text, back ));
	EXPECT_TRUE( back.empty() );
}

TEST( ContinueHistory, ARecordWithNothingInItIsNotWrittenOut )
{
	std::vector<ContinueRecord> history;
	history.push_back( Server( "a:1", 1 ));
	history.push_back( ContinueRecord() );

	std::vector<ContinueRecord> back;
	ASSERT_TRUE( ParseContinueHistory( SerialiseContinueHistory( history ), back ));
	ASSERT_EQ( 1u, back.size() );
}

TEST( ContinueHistory, SomethingElseEntirelyIsNotAHistory )
{
	std::vector<ContinueRecord> back;

	EXPECT_FALSE( ParseContinueHistory( "", back ));
	EXPECT_FALSE( ParseContinueHistory( "hello\n", back ));
	EXPECT_FALSE( ParseContinueHistory( "fua-continue 1\nkind server\naddress a:1\n", back ));
}

TEST( ContinueHistory, AHistoryFromANewerBuildIsRefused )
{
	// Read hopefully, a field whose meaning changed would put the player somewhere plausible and
	// wrong. The same rule one record has always followed.
	std::vector<ContinueRecord> back;

	EXPECT_FALSE( ParseContinueHistory( "fua-continue-history 99\nentry\nkind server\naddress a:1\n", back ));
	EXPECT_FALSE( ParseContinueHistory( "fua-continue-history 0\nentry\nkind server\naddress a:1\n", back ));
	EXPECT_FALSE( ParseContinueHistory( "fua-continue-history x\n", back ));
}

TEST( ContinueHistory, OneBadEntryCostsOneRow )
{
	// The file holds fifty where it used to hold one, so all-or-nothing parsing would let a single
	// mangled entry throw away the other forty-nine.
	const char *const text =
		"fua-continue-history 1\n"
		"entry\n"
		"kind server\n"
		"address good:1\n"
		"entry\n"
		"kind single\n"				// a Single with no save path is not a session
		"map MAP01\n"
		"entry\n"
		"kind server\n"
		"address alsogood:1\n";

	std::vector<ContinueRecord> back;
	ASSERT_TRUE( ParseContinueHistory( text, back ));

	ASSERT_EQ( 2u, back.size() );
	EXPECT_EQ( "good:1", back[0].address );
	EXPECT_EQ( "alsogood:1", back[1].address );
}

TEST( ContinueHistory, LinesBeforeTheFirstEntryAreNotAnEntry )
{
	const char *const text =
		"fua-continue-history 1\n"
		"address stray:1\n"			// not inside any entry, so it belongs to nothing
		"entry\n"
		"kind server\n"
		"address real:1\n";

	std::vector<ContinueRecord> back;
	ASSERT_TRUE( ParseContinueHistory( text, back ));

	ASSERT_EQ( 1u, back.size() );
	EXPECT_EQ( "real:1", back[0].address );
}

TEST( ContinueHistory, AFullHistorySurvivesTheRoundTrip )
{
	// Fifty entries is the most a player can ask for, and it is the size at which a format that
	// separates entries badly would first show it.
	std::vector<ContinueRecord> history;
	for ( int i = 1; i <= kContinueHistoryMax; ++i )
	{
		char address[32];
		snprintf( address, sizeof address, "10.0.0.%d:10666", i );
		history.push_back( Server( address, i ));
	}

	std::vector<ContinueRecord> back;
	ASSERT_TRUE( ParseContinueHistory( SerialiseContinueHistory( history ), back ));
	ASSERT_EQ( static_cast<size_t>( kContinueHistoryMax ), back.size() );
	EXPECT_EQ( "10.0.0.50:10666", back[49].address );
}

TEST( ContinueHistory, TheSameFileSpelledDifferentlyIsTheSameGame )
{
	// The bug this encodes, found by rehosting from the list: the row held "freedoom2.wad" because
	// that is what the player picked, and the config the RUNNING server reported held the absolute
	// path the engine had resolved it to. Compared as strings that is two games, so rehosting added a
	// second copy of the row AND left the pill offering to take the player back to the game they were
	// already standing in.
	ContinueRecord picked = Hosted( "MAP01", 1 );
	ContinueRecord running = Hosted( "MAP01", 2 );
	running.host.iwad = "/Users/someone/games/Doom2.WAD";

	EXPECT_EQ( ContinueIdentity( picked ), ContinueIdentity( running ));

	// And the list agrees: one row, not two.
	std::vector<ContinueRecord> history;
	history.push_back( picked );
	EXPECT_EQ( 1u, InsertContinueEntry( history, running, 10 ).size() );
}

TEST( ContinueHistory, APathIsNotPartOfWhatAMapWasPlayedWith )
{
	// Same rule for the other kinds: where a file happens to live on this disk is not what makes a
	// session the session.
	ContinueRecord bare = Single( "MAP01", "sunder.wad", 1 );

	ContinueRecord pathed = Single( "MAP01", "sunder.wad", 2 );
	pathed.iwad = "C:\\Games\\Doom\\doom2.wad";
	pathed.wads[0].name = "/home/someone/wads/Sunder.wad";

	EXPECT_EQ( ContinueIdentity( bare ), ContinueIdentity( pathed ));
}

TEST( ContinueHistory, DifferentFilesAreStillDifferentGames )
{
	// The fix must not make everything the same thing: only the directory is ignored, never the name.
	ContinueRecord a = Hosted( "MAP01", 1 );
	a.host.iwad = "/games/doom2.wad";

	ContinueRecord b = Hosted( "MAP01", 1 );
	b.host.iwad = "/games/tnt.wad";

	EXPECT_NE( ContinueIdentity( a ), ContinueIdentity( b ));
}
