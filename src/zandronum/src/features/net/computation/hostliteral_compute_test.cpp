// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/net/computation/hostliteral_compute.h"

using namespace zx;

namespace
{

HostLiteral Classify( const char *host, char *out )
{
	return ClassifyHost( host, out, 512 );
}

} // namespace

TEST( ClassifyHost, ADottedQuadNeedsNoLookup )
{
	// The one that mattered: the NAT lab points every engine at a literal, and asking the resolver
	// for its AAAA stalled the browser until the server list timed out empty.
	char out[512];
	EXPECT_EQ( HostLiteral::V4, Classify( "203.0.113.10", out ));
	EXPECT_STREQ( "203.0.113.10", out );

	EXPECT_EQ( HostLiteral::V4, Classify( "0.0.0.0", out ));
	EXPECT_EQ( HostLiteral::V4, Classify( "255.255.255.255", out ));
}

TEST( ClassifyHost, APortIsStrippedFromEitherKindOfLiteral )
{
	char out[512];

	EXPECT_EQ( HostLiteral::V4, Classify( "203.0.113.10:15300", out ));
	EXPECT_STREQ( "203.0.113.10", out );

	EXPECT_EQ( HostLiteral::Name, Classify( "registry.example.net:15300", out ));
	EXPECT_STREQ( "registry.example.net", out );
}

TEST( ClassifyHost, ABracketedAddressIsV6AndLosesItsBrackets )
{
	char out[512];

	EXPECT_EQ( HostLiteral::V6, Classify( "[2001:db8::1]", out ));
	EXPECT_STREQ( "2001:db8::1", out );

	EXPECT_EQ( HostLiteral::V6, Classify( "[2001:db8::1]:15300", out ));
	EXPECT_STREQ( "2001:db8::1", out );
}

TEST( ClassifyHost, ABareV6LiteralSurvivesIntact )
{
	// The old code cut at the first colon, which turns every unbracketed v6 address into the string
	// "2001" and resolves it to whatever that happens to name.
	char out[512];

	EXPECT_EQ( HostLiteral::V6, Classify( "2001:db8::1", out ));
	EXPECT_STREQ( "2001:db8::1", out );

	EXPECT_EQ( HostLiteral::V6, Classify( "::1", out ));
	EXPECT_STREQ( "::1", out );
}

TEST( ClassifyHost, AnOrdinaryNameIsWorthALookup )
{
	char out[512];

	EXPECT_EQ( HostLiteral::Name, Classify( "registry.cantstopscrolling.net", out ));
	EXPECT_STREQ( "registry.cantstopscrolling.net", out );

	EXPECT_EQ( HostLiteral::Name, Classify( "localhost", out ));
}

TEST( ClassifyHost, ThingsShapedLikeAQuadButNotOneAreNames )
{
	// A name is the safe answer for anything ambiguous, because the cost is a lookup that finds
	// nothing rather than an address pointed at the wrong machine.
	char out[512];

	EXPECT_EQ( HostLiteral::Name, Classify( "203.0.113", out ));			// too few fields
	EXPECT_EQ( HostLiteral::Name, Classify( "203.0.113.10.7", out ));		// too many
	EXPECT_EQ( HostLiteral::Name, Classify( "203.0.113.256", out ));		// out of range
	EXPECT_EQ( HostLiteral::Name, Classify( "203.0.113.0010", out ));		// too many digits
	EXPECT_EQ( HostLiteral::Name, Classify( "203.0.113.", out ));			// trailing dot
	EXPECT_EQ( HostLiteral::Name, Classify( "203..113.10", out ));			// empty field
	EXPECT_EQ( HostLiteral::Name, Classify( "203.0.113.1a", out ));			// trailing rubbish
	EXPECT_EQ( HostLiteral::Name, Classify( "example4.com", out ));
}

TEST( ClassifyHost, NothingUsableReadsAsAName )
{
	char out[512];

	EXPECT_EQ( HostLiteral::Name, Classify( 0, out ));
	EXPECT_EQ( HostLiteral::Name, Classify( "", out ));
	EXPECT_EQ( HostLiteral::Name, Classify( ":15300", out ));		// a port and no host
	EXPECT_EQ( HostLiteral::Name, Classify( "[2001:db8::1", out ));	// never closed
	EXPECT_EQ( HostLiteral::Name, Classify( "[]", out ));			// closed around nothing

	EXPECT_EQ( HostLiteral::Name, ClassifyHost( "example.net", 0, 512 ));
	EXPECT_EQ( HostLiteral::Name, ClassifyHost( "example.net", out, 0 ));
}

TEST( ClassifyHost, SomethingTooLongToHoldIsRefusedRatherThanCut )
{
	// Truncating would hand back a shorter name that may well resolve, which is worse than failing.
	char out[16];
	EXPECT_EQ( HostLiteral::Name, ClassifyHost( "a-very-long-hostname.example.net", out, sizeof out ));
	EXPECT_STREQ( "", out );

	EXPECT_EQ( HostLiteral::Name, ClassifyHost( "[2001:db8::dead:beef:cafe]", out, sizeof out ));
}
