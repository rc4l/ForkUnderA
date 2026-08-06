// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/port-mapping/computation/ssdp_compute.h"

using zx::BuildSsdpSearch;
using zx::HeaderValue;
using zx::HttpUrl;
using zx::IsAcceptableLocation;
using zx::IsPrivateIPv4;
using zx::LocationFromSsdpReply;
using zx::ParseHttpUrl;
using zx::ResolveUrl;
using std::string;

namespace
{
// A reply of the shape real firmware sends, down to the inconsistent capitalisation.
const char *const kReply =
	"HTTP/1.1 200 OK\r\n"
	"CACHE-CONTROL: max-age=120\r\n"
	"ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
	"Location: http://192.168.1.1:5000/rootDesc.xml\r\n"
	"SERVER: Linux/3.4 UPnP/1.0 MiniUPnPd/1.9\r\n"
	"\r\n";
} // namespace

// ---------------------------------------------------------------- the search

TEST( SsdpSearch, IsAWellFormedMSearch )
{
	const string search = BuildSsdpSearch( "upnp:rootdevice", 2 );

	EXPECT_EQ( 0u, search.find( "M-SEARCH * HTTP/1.1\r\n" ));
	EXPECT_NE( string::npos, search.find( "HOST: 239.255.255.250:1900\r\n" ));
	EXPECT_NE( string::npos, search.find( "MAN: \"ssdp:discover\"\r\n" ));
	EXPECT_NE( string::npos, search.find( "ST: upnp:rootdevice\r\n" ));

	// The blank line is what ends the request; without it a gateway waits for more.
	EXPECT_NE( string::npos, search.find( "\r\n\r\n" ));
}

TEST( SsdpSearch, ClampsTheWaitToSomethingAPlayerWillSitThrough )
{
	// MX is how long routers may stagger replies over. A large one is polite to the network and rude
	// to the person watching a spinner.
	EXPECT_NE( string::npos, BuildSsdpSearch( "x", 99 ).find( "MX: 5\r\n" ));
	EXPECT_NE( string::npos, BuildSsdpSearch( "x", 0 ).find( "MX: 1\r\n" ));
	EXPECT_NE( string::npos, BuildSsdpSearch( "x", -4 ).find( "MX: 1\r\n" ));
	EXPECT_NE( string::npos, BuildSsdpSearch( "x", 3 ).find( "MX: 3\r\n" ));
}

// ---------------------------------------------------------------- headers

TEST( SsdpHeaders, FindsAValueWhateverTheCase )
{
	// Written by router firmware, and every vendor picks its own capitalisation.
	EXPECT_EQ( "http://192.168.1.1:5000/rootDesc.xml", HeaderValue( kReply, "LOCATION" ));
	EXPECT_EQ( "http://192.168.1.1:5000/rootDesc.xml", HeaderValue( kReply, "location" ));
	EXPECT_EQ( "http://192.168.1.1:5000/rootDesc.xml", HeaderValue( kReply, "LoCaTiOn" ));
}

TEST( SsdpHeaders, TrimsTheSpaceAfterTheColon )
{
	EXPECT_EQ( "max-age=120", HeaderValue( kReply, "CACHE-CONTROL" ));
	EXPECT_EQ( "value", HeaderValue( "X:value\r\n", "X" ));
	EXPECT_EQ( "value", HeaderValue( "X: \t value \t \r\n", "X" ));
}

TEST( SsdpHeaders, AbsentIsEmptyRatherThanSomethingElse )
{
	EXPECT_EQ( "", HeaderValue( kReply, "NOTHERE" ));
	EXPECT_EQ( "", HeaderValue( "", "LOCATION" ));
	EXPECT_EQ( "", HeaderValue( "no colon on this line\r\n", "LOCATION" ));
}

TEST( SsdpHeaders, ReadsALastLineWithNoNewlineAfterIt )
{
	// Datagrams are not obliged to be tidy.
	EXPECT_EQ( "here", HeaderValue( "A: one\r\nB: here", "B" ));
}

// ---------------------------------------------------------------- URLs

TEST( HttpUrlParsing, SplitsHostPortAndPath )
{
	const HttpUrl url = ParseHttpUrl( "http://192.168.1.1:5000/rootDesc.xml" );

	ASSERT_TRUE( url.valid );
	EXPECT_EQ( "192.168.1.1", url.host );
	EXPECT_EQ( 5000, url.port );
	EXPECT_EQ( "/rootDesc.xml", url.path );
}

TEST( HttpUrlParsing, DefaultsThePortAndThePath )
{
	const HttpUrl url = ParseHttpUrl( "http://10.0.0.1" );

	ASSERT_TRUE( url.valid );
	EXPECT_EQ( 80, url.port );
	EXPECT_EQ( "/", url.path );
}

TEST( HttpUrlParsing, KeepsAQueryWithThePath )
{
	const HttpUrl url = ParseHttpUrl( "http://10.0.0.1/desc?v=2" );

	ASSERT_TRUE( url.valid );
	EXPECT_EQ( "/desc?v=2", url.path );
}

TEST( HttpUrlParsing, GivesAQueryWithNoPathASlashToHangFrom )
{
	// [rc4l] Unusual but legal, and firmware does emit it. The authority ends at the '?' rather than
	// at a '/', so the remainder starts with '?' -- and a request line reading `POST ?v=2 HTTP/1.1`
	// is malformed. The target has to be a path, so it gets one.
	const HttpUrl url = ParseHttpUrl( "http://10.0.0.1?v=2" );

	ASSERT_TRUE( url.valid );
	EXPECT_EQ( "10.0.0.1", url.host );
	EXPECT_EQ( "/?v=2", url.path );

	// Same for a fragment, which is the other delimiter that ends an authority.
	EXPECT_EQ( "/#frag", ParseHttpUrl( "http://10.0.0.1#frag" ).path );
}

TEST( HttpUrlParsing, RefusesWhatItCannotSafelyRead )
{
	// https would mean TLS to a device with a self-signed certificate, which is a negotiation
	// nobody wins. The rest are simply not URLs we should be fetching.
	EXPECT_FALSE( ParseHttpUrl( "https://192.168.1.1/x" ).valid );
	EXPECT_FALSE( ParseHttpUrl( "ftp://192.168.1.1/x" ).valid );
	EXPECT_FALSE( ParseHttpUrl( "192.168.1.1/x" ).valid );
	EXPECT_FALSE( ParseHttpUrl( "" ).valid );
	EXPECT_FALSE( ParseHttpUrl( "http://" ).valid );
	EXPECT_FALSE( ParseHttpUrl( "http:///onlyapath" ).valid );
	EXPECT_FALSE( ParseHttpUrl( "http://:5000/x" ).valid );
}

TEST( HttpUrlParsing, RefusesAPortThatIsNotOne )
{
	EXPECT_FALSE( ParseHttpUrl( "http://192.168.1.1:abc/x" ).valid );
	EXPECT_FALSE( ParseHttpUrl( "http://192.168.1.1:0/x" ).valid );
	EXPECT_FALSE( ParseHttpUrl( "http://192.168.1.1:65536/x" ).valid );
	EXPECT_FALSE( ParseHttpUrl( "http://192.168.1.1:/x" ).valid );
	EXPECT_TRUE( ParseHttpUrl( "http://192.168.1.1:65535/x" ).valid );
}

// ---------------------------------------------------------------- private addresses

TEST( PrivateAddress, AcceptsTheThreeRfc1918RangesAndLinkLocal )
{
	EXPECT_TRUE( IsPrivateIPv4( "10.0.0.1" ));
	EXPECT_TRUE( IsPrivateIPv4( "10.255.255.255" ));
	EXPECT_TRUE( IsPrivateIPv4( "172.16.0.1" ));
	EXPECT_TRUE( IsPrivateIPv4( "172.31.255.1" ));
	EXPECT_TRUE( IsPrivateIPv4( "192.168.1.1" ));

	// A router handing out no DHCP still answers here, and refusing it would fail the one case
	// where a player most needs the help.
	EXPECT_TRUE( IsPrivateIPv4( "169.254.1.1" ));
}

TEST( PrivateAddress, RejectsPublicOnes )
{
	EXPECT_FALSE( IsPrivateIPv4( "8.8.8.8" ));
	EXPECT_FALSE( IsPrivateIPv4( "1.1.1.1" ));

	// The edges of the 172 block, which is the range everyone gets wrong.
	EXPECT_FALSE( IsPrivateIPv4( "172.15.0.1" ));
	EXPECT_FALSE( IsPrivateIPv4( "172.32.0.1" ));
	EXPECT_FALSE( IsPrivateIPv4( "191.168.1.1" ));
}

TEST( PrivateAddress, RejectsAnythingThatIsNotADottedQuad )
{
	EXPECT_FALSE( IsPrivateIPv4( "" ));
	EXPECT_FALSE( IsPrivateIPv4( "10.0.0" ));
	EXPECT_FALSE( IsPrivateIPv4( "10.0.0.1.5" ));
	EXPECT_FALSE( IsPrivateIPv4( "10.0.0.256" ));
	EXPECT_FALSE( IsPrivateIPv4( "10.0.0.0001" ));
	EXPECT_FALSE( IsPrivateIPv4( "10.0.0.x" ));
	EXPECT_FALSE( IsPrivateIPv4( "router.local" ));
	EXPECT_FALSE( IsPrivateIPv4( "10..0.1" ));
}

// ---------------------------------------------------------------- the boundary that matters

TEST( SsdpLocation, AcceptsAnOrdinaryRouter )
{
	EXPECT_TRUE( IsAcceptableLocation( "http://192.168.1.1:5000/rootDesc.xml" ));
	EXPECT_EQ( "http://192.168.1.1:5000/rootDesc.xml", LocationFromSsdpReply( kReply ));
}

TEST( SsdpLocation, RefusesToBeAimedAtTheInternet )
{
	// [rc4l] The attack this whole unit exists for. Anything on the LAN can answer an M-SEARCH, and
	// a client that fetches whatever comes back is a request-forger sitting inside the network --
	// point it somewhere and the game goes there, from an address that is trusted.
	EXPECT_FALSE( IsAcceptableLocation( "http://evil.example.com/x" ));
	EXPECT_FALSE( IsAcceptableLocation( "http://8.8.8.8/x" ));

	const char *const hostile =
		"HTTP/1.1 200 OK\r\n"
		"LOCATION: http://attacker.example.com/rootDesc.xml\r\n"
		"\r\n";
	EXPECT_EQ( "", LocationFromSsdpReply( hostile ));
}

TEST( SsdpLocation, RefusesSchemesAndSizesItShouldNotFetch )
{
	EXPECT_FALSE( IsAcceptableLocation( "" ));
	EXPECT_FALSE( IsAcceptableLocation( "https://192.168.1.1/x" ));
	EXPECT_FALSE( IsAcceptableLocation( "file:///etc/passwd" ));

	// A device answering with a megabyte of nonsense is refused rather than parsed.
	EXPECT_FALSE( IsAcceptableLocation( "http://192.168.1.1/" + string( 600, 'a' )));
}

TEST( SsdpLocation, AReplyWithNoLocationYieldsNothing )
{
	EXPECT_EQ( "", LocationFromSsdpReply( "HTTP/1.1 200 OK\r\n\r\n" ));
	EXPECT_EQ( "", LocationFromSsdpReply( "" ));
}

// ---------------------------------------------------------------- resolving control URLs

TEST( ResolveControlUrl, HandlesTheThreeFormsShippedFirmwareUses )
{
	const string base = "http://192.168.1.1:5000/desc/root.xml";

	// Root-relative, which is much the commonest.
	EXPECT_EQ( "http://192.168.1.1:5000/ctl/IPConn", ResolveUrl( base, "/ctl/IPConn" ));

	// Absolute.
	EXPECT_EQ( "http://192.168.1.1:80/x", ResolveUrl( base, "http://192.168.1.1:80/x" ));

	// Relative to the description's own directory.
	EXPECT_EQ( "http://192.168.1.1:5000/desc/ctl", ResolveUrl( base, "ctl" ));
}

TEST( ResolveControlUrl, OmitsThePortWhenItIsTheDefault )
{
	EXPECT_EQ( "http://10.0.0.1/ctl", ResolveUrl( "http://10.0.0.1/root.xml", "/ctl" ));
}

TEST( ResolveControlUrl, HandlesABaseWithNoDirectoryPart )
{
	EXPECT_EQ( "http://10.0.0.1/ctl", ResolveUrl( "http://10.0.0.1", "ctl" ));
}

TEST( ResolveControlUrl, GivesNothingBackFromNothing )
{
	EXPECT_EQ( "", ResolveUrl( "http://10.0.0.1/root.xml", "" ));
	EXPECT_EQ( "", ResolveUrl( "not a url", "/ctl" ));
}
