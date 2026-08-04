// [rc4l] Tests for the server registry list parser. Every line/branch (the coverage gate enforces
// 100% on *_compute.cpp).
//
// The stakes here are narrow but sharp: this parser is the only thing standing between an HTTPS body
// we did not author and the list of server registries the client will talk to. The adversarial cases
// below (challenge pages, error bodies, truncated fetches) matter more than the happy path, because
// the happy path fails loudly and those fail silently.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/federated-server-registry/computation/serverregistrylist_compute.h"

#include <gtest/gtest.h>

using zx::IsValidServerRegistryHost;
using zx::MergeServerRegistryLists;
using zx::ParseServerRegistryCSV;
using zx::ParseServerRegistryList;
using zx::ServerRegistryEntry;

namespace
{
// The file as shipped, near enough: comments, a blank line, one real entry.
const char *kShippedFile =
	"# ZandroX server registries\n"
	"# format-version: 1\n"
	"\n"
	"registry.cantstopscrolling.net    rc4l\n";
}

// ---- IsValidServerRegistryHost: the guard that makes an HTML body parse to nothing ----

TEST( ServerRegistryHost, AcceptsRealHostnamesAndAddresses )
{
	EXPECT_TRUE( IsValidServerRegistryHost( "registry.cantstopscrolling.net" ) );
	EXPECT_TRUE( IsValidServerRegistryHost( "localhost" ) );           // single label, no dot
	EXPECT_TRUE( IsValidServerRegistryHost( "127.0.0.1" ) );           // dotted IPv4 falls out free
	EXPECT_TRUE( IsValidServerRegistryHost( "a-b.example-1.co.uk" ) ); // interior hyphens, many labels
	EXPECT_TRUE( IsValidServerRegistryHost( "REGISTRY.EXAMPLE.NET" ) );// case is not our business
}

TEST( ServerRegistryHost, RejectsMalformedNames )
{
	EXPECT_FALSE( IsValidServerRegistryHost( "" ) );
	EXPECT_FALSE( IsValidServerRegistryHost( "example..net" ) );    // empty label
	EXPECT_FALSE( IsValidServerRegistryHost( ".example.net" ) );    // leading dot -> empty first label
	EXPECT_FALSE( IsValidServerRegistryHost( "example.net." ) );    // trailing dot -> empty last label
	EXPECT_FALSE( IsValidServerRegistryHost( "-example.net" ) );    // label starts with a hyphen
	EXPECT_FALSE( IsValidServerRegistryHost( "example-.net" ) );    // label ends with a hyphen
	EXPECT_FALSE( IsValidServerRegistryHost( "under_score.net" ) ); // not legal in a hostname
	EXPECT_FALSE( IsValidServerRegistryHost( "exa mple.net" ) );    // embedded space
	EXPECT_FALSE( IsValidServerRegistryHost( std::string( 64, 'a' ) + ".net" ) ); // label over 63
	EXPECT_FALSE( IsValidServerRegistryHost( std::string( 254, 'a' ) ) );         // name over 253
}

TEST( ServerRegistryHost, RejectsHtmlSoAChallengePageParsesToNothing )
{
	// The Brazil case: a CDN interstitial arrives where a list was expected. None of it is a host.
	EXPECT_FALSE( IsValidServerRegistryHost( "<!DOCTYPE" ) );
	EXPECT_FALSE( IsValidServerRegistryHost( "<html>" ) );
	EXPECT_FALSE( IsValidServerRegistryHost( "404:" ) ); // not a host either, once the port splits off
}

// ---- ParseServerRegistryList ------------------------------------------------

TEST( ParseServerRegistryList, ReadsTheShippedFile )
{
	const std::vector<ServerRegistryEntry> got = ParseServerRegistryList( kShippedFile );
	ASSERT_EQ( got.size( ), 1u );
	EXPECT_EQ( got[0].host, "registry.cantstopscrolling.net" );
	EXPECT_EQ( got[0].port, 0 ); // no port on the line -> caller applies its default
	EXPECT_EQ( got[0].name, "rc4l" );
}

TEST( ParseServerRegistryList, HandlesPortsNamesAndMissingNames )
{
	const std::vector<ServerRegistryEntry> got = ParseServerRegistryList(
		"a.example.net:15300\tAlpha Registry\n"
		"b.example.net\n"                      // no port, no name
		"c.example.net:1   \n" );              // lowest legal port, trailing space only
	ASSERT_EQ( got.size( ), 3u );

	EXPECT_EQ( got[0].host, "a.example.net" );
	EXPECT_EQ( got[0].port, 15300 );
	EXPECT_EQ( got[0].name, "Alpha Registry" ); // name keeps its interior space

	EXPECT_EQ( got[1].host, "b.example.net" );
	EXPECT_EQ( got[1].port, 0 );
	EXPECT_EQ( got[1].name, "" );

	EXPECT_EQ( got[2].port, 1 );
	EXPECT_EQ( got[2].name, "" );
}

TEST( ParseServerRegistryList, SkipsBadLinesWithoutLosingGoodOnes )
{
	// One bad entry costs its own listing and nothing else -- that is the whole design.
	const std::vector<ServerRegistryEntry> got = ParseServerRegistryList(
		"good-one.example.net  Keep\n"
		"bad_host.example.net  underscore\n"
		"a.example.net:       missing port digits\n"
		"a.example.net:0      port zero\n"
		"a.example.net:65536  port over range\n"
		"a.example.net:123456 too many port digits\n"
		"a.example.net:80x    non-digit in port\n"
		"good-two.example.net Keep\n" );
	ASSERT_EQ( got.size( ), 2u );
	EXPECT_EQ( got[0].host, "good-one.example.net" );
	EXPECT_EQ( got[1].host, "good-two.example.net" );
}

TEST( ParseServerRegistryList, IgnoresCommentsAndBlankLinesButNotMidLineHashes )
{
	const std::vector<ServerRegistryEntry> got = ParseServerRegistryList(
		"# a comment\n"
		"   # an indented comment\n"
		"\n"
		"   \n"
		"a.example.net  Server #1\n" ); // '#' inside a display name is text, not a comment
	ASSERT_EQ( got.size( ), 1u );
	EXPECT_EQ( got[0].name, "Server #1" );
}

TEST( ParseServerRegistryList, ToleratesCrlfBomAndAMissingFinalNewline )
{
	// GitHub raw serves LF, but the file is editable by anyone and a fetch can arrive however it
	// arrives. None of these should cost an entry.
	const std::vector<ServerRegistryEntry> crlf =
		ParseServerRegistryList( "a.example.net\tAlpha\r\nb.example.net\r\n" );
	ASSERT_EQ( crlf.size( ), 2u );
	EXPECT_EQ( crlf[0].name, "Alpha" ); // the CR is not part of the name

	const std::vector<ServerRegistryEntry> bom =
		ParseServerRegistryList( "\xEF\xBB\xBF" "a.example.net\n" );
	ASSERT_EQ( bom.size( ), 1u );
	EXPECT_EQ( bom[0].host, "a.example.net" ); // BOM did not glue itself to the first host

	const std::vector<ServerRegistryEntry> noEol = ParseServerRegistryList( "a.example.net  Alpha" );
	ASSERT_EQ( noEol.size( ), 1u );
	EXPECT_EQ( noEol[0].name, "Alpha" );
}

TEST( ParseServerRegistryList, CollapsesDuplicateHostsKeepingTheFirst )
{
	const std::vector<ServerRegistryEntry> got = ParseServerRegistryList(
		"a.example.net:15300  First\n"
		"a.example.net:16000  Second\n"
		"A.EXAMPLE.NET        Third\n" ); // hostnames are case-insensitive
	ASSERT_EQ( got.size( ), 1u );
	EXPECT_EQ( got[0].port, 15300 );
	EXPECT_EQ( got[0].name, "First" );
}

TEST( ParseServerRegistryList, EmptyAndNonListBodiesYieldNothing )
{
	// Every one of these must come back empty so the caller can say "failed fetch, keep the cache"
	// rather than committing an empty list over a working one.
	EXPECT_TRUE( ParseServerRegistryList( "" ).empty( ) );
	EXPECT_TRUE( ParseServerRegistryList( "\n\n   \n" ).empty( ) );
	EXPECT_TRUE( ParseServerRegistryList( "# only comments\n" ).empty( ) );
	EXPECT_TRUE( ParseServerRegistryList( "404: Not Found\n" ).empty( ) );
	EXPECT_TRUE( ParseServerRegistryList(
		"<!DOCTYPE html>\n<html><head><title>Just a moment...</title></head>\n"
		"<body>Checking your browser before accessing the site.</body></html>\n" ).empty( ) );
}

TEST( ParseServerRegistryList, ATruncatedFetchKeepsWhatFullyArrived )
{
	// Connection dropped mid-file: the last line is a fragment. It either parses as a host or it does
	// not, but it can never corrupt the entries above it.
	const std::vector<ServerRegistryEntry> got = ParseServerRegistryList(
		"a.example.net  Alpha\n"
		"b.example.net:153" ); // cut mid-port
	ASSERT_EQ( got.size( ), 2u );
	EXPECT_EQ( got[1].port, 153 ); // a legal port that happens to be wrong -- indistinguishable, and
	                               // harmless: the client just fails to reach it and moves on
}

// ---- ParseServerRegistryCSV: the cl_fua_serverregistry_list CVAR ------------

TEST( ParseServerRegistryCSV, SplitsTrimsAndAppliesTheSameHostRules )
{
	const std::vector<ServerRegistryEntry> got =
		ParseServerRegistryCSV( " a.example.net , b.example.net:15300 ,bad_host.net" );
	ASSERT_EQ( got.size( ), 2u );
	EXPECT_EQ( got[0].host, "a.example.net" );
	EXPECT_EQ( got[0].port, 0 );
	EXPECT_EQ( got[1].host, "b.example.net" );
	EXPECT_EQ( got[1].port, 15300 );
	EXPECT_EQ( got[1].name, "" ); // a CVAR carries no display names
}

TEST( ParseServerRegistryCSV, ForgivesHandTypedSloppiness )
{
	// Empty fields, stray whitespace of every kind, and a trailing comma are all normal in something
	// a person typed into a console.
	const std::vector<ServerRegistryEntry> got =
		ParseServerRegistryCSV( "a.example.net,, \n\t b.example.net \v\f," );
	ASSERT_EQ( got.size( ), 2u );
	EXPECT_EQ( got[1].host, "b.example.net" );

	EXPECT_TRUE( ParseServerRegistryCSV( "" ).empty( ) );
	EXPECT_TRUE( ParseServerRegistryCSV( "   " ).empty( ) );
	EXPECT_TRUE( ParseServerRegistryCSV( ",,," ).empty( ) );
}

TEST( ParseServerRegistryCSV, SingleEntryAndDuplicates )
{
	const std::vector<ServerRegistryEntry> one = ParseServerRegistryCSV( "a.example.net" ); // no comma
	ASSERT_EQ( one.size( ), 1u );

	const std::vector<ServerRegistryEntry> dup = ParseServerRegistryCSV( "a.example.net:1,A.example.net:2" );
	ASSERT_EQ( dup.size( ), 1u );
	EXPECT_EQ( dup[0].port, 1 ); // first wins
}

// ---- MergeServerRegistryLists ----------------------------------------------

TEST( MergeServerRegistryLists, UserEntriesComeFirstAndWinOnConflict )
{
	const std::vector<ServerRegistryEntry> user =
		ParseServerRegistryCSV( "mine.example.net,shared.example.net:9999" );
	const std::vector<ServerRegistryEntry> shipped = ParseServerRegistryList(
		"shared.example.net:15300  Shared\n"
		"theirs.example.net        Theirs\n" );

	const std::vector<ServerRegistryEntry> got = MergeServerRegistryLists( user, shipped );
	ASSERT_EQ( got.size( ), 3u );
	EXPECT_EQ( got[0].host, "mine.example.net" );
	EXPECT_EQ( got[1].host, "shared.example.net" );
	EXPECT_EQ( got[1].port, 9999 ); // the user's port, not the shipped one
	EXPECT_EQ( got[2].host, "theirs.example.net" );
}

TEST( MergeServerRegistryLists, EitherSideMayBeEmpty )
{
	const std::vector<ServerRegistryEntry> shipped = ParseServerRegistryList( kShippedFile );
	const std::vector<ServerRegistryEntry> none;

	// The ordinary case: player set no CVAR, so they get exactly what we shipped.
	EXPECT_EQ( MergeServerRegistryLists( none, shipped ).size( ), 1u );
	// The other ordinary case: fetch failed and there is no cache, so only the player's own remain.
	EXPECT_EQ( MergeServerRegistryLists( shipped, none ).size( ), 1u );
	EXPECT_TRUE( MergeServerRegistryLists( none, none ).empty( ) );
}

TEST( MergeServerRegistryLists, DuplicatesWithinOneSideAreCollapsedToo )
{
	std::vector<ServerRegistryEntry> user( 2 );
	user[0].host = "a.example.net";
	user[0].port = 1;
	user[1].host = "a.example.net"; // constructed directly: a caller could hand us anything
	user[1].port = 2;

	const std::vector<ServerRegistryEntry> got =
		MergeServerRegistryLists( user, std::vector<ServerRegistryEntry>( ) );
	ASSERT_EQ( got.size( ), 1u );
	EXPECT_EQ( got[0].port, 1 );
}
