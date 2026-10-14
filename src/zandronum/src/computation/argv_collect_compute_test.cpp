// [rc4l] Regression tests for the macOS command-line collection.
//
// The bug these exist for: the entry point collected argv into a fixed 64-entry static array with no
// bounds check. A player double-clicking the app never noticed; a server started from the HOST tab is
// handed one argument per gameplay cvar, two per map in the rotation and two per WAD -- about a
// hundred and fifty -- so every argument past the sixty-fourth was stored past the end of the array
// and over the next file's statics. The server died a second or two later inside code that had
// nothing to do with the command line, and a different piece of it each run.
//
// So the count is what is pinned here, hard: an argument list far longer than any cap anyone would
// have picked must arrive whole and in order.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"

#include <string>
#include <vector>

#include "computation/argv_collect_compute.h"

namespace
{
// argv as the OS hands it over: a NULL-terminated vector of char*, with the terminator present so a
// bound that reads argv[argc] would find something to trip over.
std::vector<const char *> MakeArgv( const std::vector<std::string> &args )
{
	std::vector<const char *> out;
	for ( size_t i = 0; i < args.size( ); ++i )
		out.push_back( args[i].c_str( ));
	out.push_back( NULL );
	return out;
}
} // namespace

// [rc4l] An ordinary command line survives unchanged and in order.
TEST(ArgvCollect, KeepsEveryArgumentInOrder)
{
	std::vector<std::string> args;
	args.push_back( "./forkundera" );
	args.push_back( "-iwad" );
	args.push_back( "freedoom2.wad" );
	args.push_back( "+map" );
	args.push_back( "MAP01" );

	std::vector<const char *> argv = MakeArgv( args );
	const zx::CollectedArgv got = zx::ComputeCollectArgv( 5, &argv[0] );

	ASSERT_EQ( size_t( 5 ), got.args.size( ));
	for ( size_t i = 0; i < args.size( ); ++i )
		EXPECT_EQ( args[i], got.args[i] ) << "at " << i;
	EXPECT_FALSE( got.bRestartedFromWadPicker );
}

// [rc4l] THE BUG. A hosted server's command line is far longer than the sixty-four the old fixed
// array held; every one of those arguments has to arrive, because the ones past the cap were what
// used to be written over the next file's statics.
TEST(ArgvCollect, AHostedServersLongCommandLineArrivesWhole)
{
	// A real HOST-tab spawn: the gameplay cvars, then a thirty-two map rotation, then the server
	// settings. Rounded up well past it, because the point is that there is no cap at all.
	std::vector<std::string> args;
	args.push_back( "./forkundera" );
	args.push_back( "-host" );
	for ( int i = 0; i < 100; ++i )
	{
		args.push_back( "+addmap" );
		args.push_back( "MAP" + std::string( 1, char( '0' + ( i % 10 ))));
	}
	args.push_back( "+sv_hostname" );
	args.push_back( "FUA Custom Server" );

	const int argc = static_cast<int>( args.size( ));
	ASSERT_GT( argc, 64 ) << "the test must exceed the old fixed cap or it proves nothing";

	std::vector<const char *> argv = MakeArgv( args );
	const zx::CollectedArgv got = zx::ComputeCollectArgv( argc, &argv[0] );

	ASSERT_EQ( args.size( ), got.args.size( ));
	// The last one matters most: it is the furthest past the old cap.
	EXPECT_EQ( args.back( ), got.args.back( ));
	for ( size_t i = 0; i < args.size( ); ++i )
		EXPECT_EQ( args[i], got.args[i] ) << "at " << i;
}

// [rc4l] argc is the bound. The old loop ran `i <= argc` and read argv[argc], the standard's NULL
// terminator -- harmless only because a NULL check caught it, which is not a reason to keep reading
// one past the end in a file whose bug was reading one past the end.
TEST(ArgvCollect, StopsAtArgcAndNeverReadsTheTerminator)
{
	std::vector<std::string> args;
	args.push_back( "./forkundera" );
	args.push_back( "-host" );

	std::vector<const char *> argv = MakeArgv( args );
	// Something real where the terminator was, so reading past argc would be visible rather than
	// silently skipped by a NULL check.
	argv.back( ) = "-iwad";

	const zx::CollectedArgv got = zx::ComputeCollectArgv( 2, &argv[0] );

	ASSERT_EQ( size_t( 2 ), got.args.size( ));
	EXPECT_EQ( "-host", got.args[1] );
}

// [rc4l] -wad_picker_restart is the launcher talking to us, not an argument for the engine.
TEST(ArgvCollect, WadPickerRestartIsReportedAndDropped)
{
	std::vector<std::string> args;
	args.push_back( "./forkundera" );
	args.push_back( "-wad_picker_restart" );
	args.push_back( "-iwad" );

	std::vector<const char *> argv = MakeArgv( args );
	const zx::CollectedArgv got = zx::ComputeCollectArgv( 3, &argv[0] );

	EXPECT_TRUE( got.bRestartedFromWadPicker );
	ASSERT_EQ( size_t( 2 ), got.args.size( ));
	EXPECT_EQ( "./forkundera", got.args[0] );
	EXPECT_EQ( "-iwad", got.args[1] );
}

// [rc4l] NULL and empty entries are both things the OS may hand over, and an empty one reaches the
// engine's parser as a stray token.
TEST(ArgvCollect, SkipsNullAndEmptyArguments)
{
	const char *raw[] = { "./forkundera", NULL, "", "-host", "" };
	const zx::CollectedArgv got = zx::ComputeCollectArgv( 5, raw );

	ASSERT_EQ( size_t( 2 ), got.args.size( ));
	EXPECT_EQ( "./forkundera", got.args[0] );
	EXPECT_EQ( "-host", got.args[1] );
}

// [rc4l] Nothing to collect is not a failure, and must not be answered by indexing an empty list.
TEST(ArgvCollect, EmptyAndNullInputsGiveAnEmptyResult)
{
	const char *raw[] = { "./forkundera" };

	const zx::CollectedArgv none = zx::ComputeCollectArgv( 0, raw );
	EXPECT_TRUE( none.args.empty( ));
	EXPECT_FALSE( none.bRestartedFromWadPicker );

	const zx::CollectedArgv negative = zx::ComputeCollectArgv( -1, raw );
	EXPECT_TRUE( negative.args.empty( ));

	const zx::CollectedArgv nullv = zx::ComputeCollectArgv( 3, NULL );
	EXPECT_TRUE( nullv.args.empty( ));
	EXPECT_FALSE( nullv.bRestartedFromWadPicker );
}
