// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/host-command/computation/hostcommand_compute.h"

using namespace zx;

namespace
{

std::vector<std::string> Args( const char *a = 0, const char *b = 0, const char *c = 0,
	const char *d = 0, const char *e = 0 )
{
	std::vector<std::string> out;
	if ( a ) out.push_back( a );
	if ( b ) out.push_back( b );
	if ( c ) out.push_back( c );
	if ( d ) out.push_back( d );
	if ( e ) out.push_back( e );
	return out;
}

} // namespace

TEST( HostCommand, AMapOnItsOwnIsEnough )
{
	HostConfig out;
	std::string error;

	ASSERT_TRUE( ParseHostCommand( Args( "MAP07" ), out, error )) << error;
	EXPECT_EQ( "MAP07", out.map );
	EXPECT_EQ( "", error );
	EXPECT_FALSE( out.hostName.empty( )) << "a server with no name at all is worse than a default";
}

TEST( HostCommand, TheOptionsAreRead )
{
	HostConfig out;
	std::string error;

	ASSERT_TRUE( ParseHostCommand( Args( "MAP07", "name", "Bob's Game" ), out, error )) << error;
	EXPECT_EQ( "Bob's Game", out.hostName );

	ASSERT_TRUE( ParseHostCommand( Args( "MAP07", "port", "10777" ), out, error )) << error;
	EXPECT_EQ( 10777, out.port );

	ASSERT_TRUE( ParseHostCommand( Args( "MAP07", "players", "16" ), out, error )) << error;
	EXPECT_EQ( 16, out.maxPlayers );

	ASSERT_TRUE( ParseHostCommand( Args( "MAP07", "file", "av.wad" ), out, error )) << error;
	ASSERT_EQ( 1u, out.pwads.size( ));
	EXPECT_EQ( "av.wad", out.pwads[0] );
}

TEST( HostCommand, NoMapIsRefusedRatherThanGuessed )
{
	HostConfig out;
	std::string error;

	EXPECT_FALSE( ParseHostCommand( Args( ), out, error ));
	EXPECT_FALSE( error.empty( )) << "a refusal with no reason is a refusal nobody can act on";
}

TEST( HostCommand, AMapNameThatCouldBeReadAsAFlagIsRefused )
{
	// The whole reason the parsing is checked rather than escaped: a value that reaches a command
	// line as another argument is a value that does something nobody typed.
	HostConfig out;
	std::string error;

	EXPECT_FALSE( ParseHostCommand( Args( "-host" ), out, error ));
	EXPECT_FALSE( ParseHostCommand( Args( "+exec" ), out, error ));
	EXPECT_FALSE( ParseHostCommand( Args( "map with spaces" ), out, error ));
}

TEST( HostCommand, AServerNameThatCannotBePassedSafelyIsRefused )
{
	HostConfig out;
	std::string error;

	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "name", "\"quoted\"" ), out, error ));
	EXPECT_FALSE( error.empty( ));
}

TEST( HostCommand, AWadMustBeAPlainFileName )
{
	// A path here would let the line name a file anywhere on the disk.
	HostConfig out;
	std::string error;

	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "file", "../secrets.wad" ), out, error ));
	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "file", "/etc/passwd" ), out, error ));
}

TEST( HostCommand, NumbersMustActuallyBeNumbers )
{
	HostConfig out;
	std::string error;

	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "port", "soon" ), out, error ));
	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "port", "-1" ), out, error ));
	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "port", "0" ), out, error ));
	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "players", "1x" ), out, error ));
}

TEST( HostCommand, AnOptionWithNothingAfterItIsAnUnfinishedLine )
{
	HostConfig out;
	std::string error;

	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "name" ), out, error ));
	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "port" ), out, error ));
}

TEST( HostCommand, AnUnknownOptionIsRefusedRatherThanIgnored )
{
	// Ignoring it would let a typo silently not do the thing the player asked for.
	HostConfig out;
	std::string error;

	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "gamemode", "dm" ), out, error ));
	EXPECT_FALSE( error.empty( ));
}

TEST( HostCommand, ARefusedParseLeavesNothingHalfBuilt )
{
	HostConfig out;
	std::string error;

	ASSERT_TRUE( ParseHostCommand( Args( "MAP07", "name", "Good" ), out, error ));
	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "port", "nope" ), out, error ));
	EXPECT_EQ( "", out.hostName ) << "the previous config survived a failed parse";
}

TEST( HostCommand, TheIwadIsNeverTakenFromTheLine )
{
	// Hosting a set you are not holding is a different feature; the caller fills this in from what
	// is actually loaded.
	HostConfig out;
	std::string error;

	ASSERT_TRUE( ParseHostCommand( Args( "MAP07" ), out, error ));
	EXPECT_EQ( "", out.iwad );
}

TEST( HostCommand, AnEmptyValueIsNotANumber )
{
	// `fua_hostmap MAP07 port ""` is a thing a console can hand over, and an empty string parsed as
	// a number is how a server ends up on port 0.
	HostConfig out;
	std::string error;

	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "port", "" ), out, error ));
	EXPECT_FALSE( ParseHostCommand( Args( "MAP07", "players", "" ), out, error ));
}
