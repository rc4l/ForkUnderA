// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include <cstring>

#include "features/net/computation/v6mapped_compute.h"

using namespace zx;

namespace
{

// ::ffff:a.b.c.d
void Mapped( unsigned char *out, unsigned char a, unsigned char b, unsigned char c, unsigned char d )
{
	memset( out, 0, 16 );
	out[10] = 0xff;
	out[11] = 0xff;
	out[12] = a;
	out[13] = b;
	out[14] = c;
	out[15] = d;
}

} // namespace

TEST(V6Mapped, RecognisesAMappedAddress)
{
	unsigned char addr[16];
	Mapped( addr, 192, 168, 0, 42 );
	EXPECT_TRUE( IsV4MappedV6( addr ));
}

TEST(V6Mapped, ExtractsTheEmbeddedAddress)
{
	unsigned char addr[16];
	Mapped( addr, 192, 168, 0, 42 );

	unsigned char four[4] = { 9, 9, 9, 9 };
	ExtractMappedV4( addr, four );

	EXPECT_EQ( 192, four[0] );
	EXPECT_EQ( 168, four[1] );
	EXPECT_EQ( 0, four[2] );
	EXPECT_EQ( 42, four[3] );
}

TEST(V6Mapped, MappedZeroIsStillMapped)
{
	// ::ffff:0.0.0.0 is a mapped address whose v4 half is zero. Reading it as "not mapped" would
	// send a peer through the v6 path on the strength of an address that is all zeroes.
	unsigned char addr[16];
	Mapped( addr, 0, 0, 0, 0 );
	EXPECT_TRUE( IsV4MappedV6( addr ));
}

TEST(V6Mapped, AllZeroesIsNotMapped)
{
	// The unspecified address. No 0xff pair, so nothing to embed.
	unsigned char addr[16];
	memset( addr, 0, sizeof( addr ));
	EXPECT_FALSE( IsV4MappedV6( addr ));
}

TEST(V6Mapped, LoopbackIsNotMapped)
{
	// ::1 is a real v6 address and must stay one.
	unsigned char addr[16];
	memset( addr, 0, sizeof( addr ));
	addr[15] = 1;
	EXPECT_FALSE( IsV4MappedV6( addr ));
}

TEST(V6Mapped, ARealV6AddressIsNotMapped)
{
	unsigned char addr[16];
	for ( int i = 0; i < 16; ++i )
		addr[i] = static_cast<unsigned char>( 0x20 + i );
	EXPECT_FALSE( IsV4MappedV6( addr ));
}

TEST(V6Mapped, TheFfffPairAloneIsNotEnough)
{
	// The dangerous false positive: a genuine v6 address carrying 0xff 0xff at bytes 10 and 11.
	// Without the zero prefix check it would be mistaken for a v4 peer and four bytes of somebody's
	// v6 address would be read as their IPv4 one.
	unsigned char addr[16];
	memset( addr, 0, sizeof( addr ));
	addr[0] = 0x20;
	addr[10] = 0xff;
	addr[11] = 0xff;
	EXPECT_FALSE( IsV4MappedV6( addr ));
}

TEST(V6Mapped, EveryByteOfThePrefixIsChecked)
{
	// One nonzero byte anywhere in the first ten disqualifies it. Checking only some of them is the
	// bug this pins.
	for ( int i = 0; i < 10; ++i )
	{
		unsigned char addr[16];
		Mapped( addr, 1, 2, 3, 4 );
		addr[i] = 1;

		EXPECT_FALSE( IsV4MappedV6( addr )) << "byte " << i << " was not checked";
	}
}

TEST(V6Mapped, BothFfffBytesAreChecked)
{
	unsigned char addr[16];

	Mapped( addr, 1, 2, 3, 4 );
	addr[10] = 0xfe;
	EXPECT_FALSE( IsV4MappedV6( addr ));

	Mapped( addr, 1, 2, 3, 4 );
	addr[11] = 0xfe;
	EXPECT_FALSE( IsV4MappedV6( addr ));
}

TEST(V6Mapped, NullIsNotMappedAndExtractingIsSafe)
{
	EXPECT_FALSE( IsV4MappedV6( 0 ));

	unsigned char four[4] = { 1, 2, 3, 4 };
	ExtractMappedV4( 0, four );
	EXPECT_EQ( 1, four[0] ) << "a refused extract must leave the caller's buffer alone";

	unsigned char addr[16];
	Mapped( addr, 5, 6, 7, 8 );
	ExtractMappedV4( addr, 0 );
}

// ---------------------------------------------------------------- putting the hat on

TEST(V6Mapped, WrapsAV4AddressForADualStackSocket)
{
	const unsigned char four[4] = { 192, 168, 1, 7 };
	unsigned char sixteen[16];
	memset( sixteen, 0xaa, sizeof( sixteen ));

	MakeV4MappedV6( four, sixteen );

	unsigned char expected[16];
	Mapped( expected, 192, 168, 1, 7 );
	EXPECT_EQ( 0, memcmp( sixteen, expected, 16 ));
}

TEST(V6Mapped, WrappingThenUnwrappingIsTheSameAddress)
{
	// The property that matters, because the two run at opposite ends of every packet: what the send
	// path dresses up, the receive path has to strip back to exactly what went in.
	static const unsigned char cases[][4] = {
		{ 0, 0, 0, 0 }, { 127, 0, 0, 1 }, { 8, 8, 8, 8 }, { 255, 255, 255, 255 },
	};

	for ( int i = 0; i < 4; ++i )
	{
		unsigned char sixteen[16];
		MakeV4MappedV6( cases[i], sixteen );

		EXPECT_TRUE( IsV4MappedV6( sixteen )) << i;

		unsigned char back[4];
		ExtractMappedV4( sixteen, back );
		EXPECT_EQ( 0, memcmp( back, cases[i], 4 )) << i;
	}
}

TEST(V6Mapped, WrappingClearsWhateverWasInTheBufferBefore)
{
	// The first ten bytes have to end up zero rather than keeping the caller's leftovers, or the
	// result stops being a mapped address and starts being a real v6 one pointed somewhere else.
	unsigned char sixteen[16];
	memset( sixteen, 0xff, sizeof( sixteen ));

	const unsigned char four[4] = { 1, 2, 3, 4 };
	MakeV4MappedV6( four, sixteen );

	for ( int i = 0; i < 10; ++i )
		EXPECT_EQ( 0, sixteen[i] ) << i;
}

TEST(V6Mapped, WrappingNullIsSafe)
{
	unsigned char sixteen[16];
	memset( sixteen, 0x11, sizeof( sixteen ));

	const unsigned char four[4] = { 1, 2, 3, 4 };
	MakeV4MappedV6( 0, sixteen );
	EXPECT_EQ( 0x11, sixteen[0] ) << "a refused wrap must leave the caller's buffer alone";

	MakeV4MappedV6( four, 0 );
}

namespace
{

// A 16-byte address from its leading bytes, zero-filled after.
void Addr( unsigned char *out, const unsigned char *lead, int leadLen )
{
	memset( out, 0, 16 );
	memcpy( out, lead, leadLen );
}

} // namespace

TEST( IsLocalV6, LinkLocalIsLocal )
{
	// fe80::/10, which is what a peer on the same LAN announces itself as.
	unsigned char addr[16];
	const unsigned char lead[2] = { 0xfe, 0x80 };
	Addr( addr, lead, 2 );
	EXPECT_TRUE( IsLocalV6( addr ));

	// The whole /10, not just fe80::/16 -- febf:: is the last address in it.
	const unsigned char top[2] = { 0xfe, 0xbf };
	Addr( addr, top, 2 );
	EXPECT_TRUE( IsLocalV6( addr ));
}

TEST( IsLocalV6, UniqueLocalIsLocalAcrossBothHalvesOfThePrefix )
{
	// fc00::/7 covers fc and fd. Reading only fd00::/8 -- the half in use today -- would leave a
	// hole the day fc00::/8 is assigned.
	unsigned char addr[16];

	const unsigned char fd[1] = { 0xfd };
	Addr( addr, fd, 1 );
	EXPECT_TRUE( IsLocalV6( addr ));

	const unsigned char fc[1] = { 0xfc };
	Addr( addr, fc, 1 );
	EXPECT_TRUE( IsLocalV6( addr ));
}

TEST( IsLocalV6, LoopbackAndUnspecifiedAreLocal )
{
	unsigned char addr[16];

	memset( addr, 0, 16 );
	EXPECT_TRUE( IsLocalV6( addr )) << "::";

	addr[15] = 1;
	EXPECT_TRUE( IsLocalV6( addr )) << "::1";
}

TEST( IsLocalV6, AnOrdinaryAddressIsNotLocal )
{
	// The one that matters: a real server must not be labelled LAN and lose its flag.
	unsigned char addr[16];
	const unsigned char lead[4] = { 0x20, 0x01, 0x0d, 0xb8 };
	Addr( addr, lead, 4 );
	EXPECT_FALSE( IsLocalV6( addr ));

	const unsigned char google[4] = { 0x26, 0x07, 0xf8, 0xb0 };
	Addr( addr, google, 4 );
	EXPECT_FALSE( IsLocalV6( addr ));
}

TEST( IsLocalV6, NeighboursOfTheLocalPrefixesAreNotLocal )
{
	// fb and fe00 sit either side of the two ranges, and an off-by-one in the masks shows up here.
	unsigned char addr[16];

	const unsigned char fb[1] = { 0xfb };
	Addr( addr, fb, 1 );
	EXPECT_FALSE( IsLocalV6( addr ));

	const unsigned char fe[2] = { 0xfe, 0x00 };
	Addr( addr, fe, 2 );
	EXPECT_FALSE( IsLocalV6( addr ));

	// fec0:: was site-local, deprecated in 2004 and outside fe80::/10.
	const unsigned char fec0[2] = { 0xfe, 0xc0 };
	Addr( addr, fec0, 2 );
	EXPECT_FALSE( IsLocalV6( addr ));
}

TEST( IsLocalV6, SomethingJustPastLoopbackIsNotLocal )
{
	// ::2 shares fifteen bytes with ::1 and is not loopback; the all-zero walk must check the last
	// byte rather than stopping when it runs out of zeroes.
	unsigned char addr[16];
	memset( addr, 0, 16 );
	addr[15] = 2;
	EXPECT_FALSE( IsLocalV6( addr ));

	memset( addr, 0, 16 );
	addr[7] = 1;
	EXPECT_FALSE( IsLocalV6( addr ));
}

TEST( IsLocalV6, NullIsRefusedRatherThanRead )
{
	EXPECT_FALSE( IsLocalV6( 0 ));
}
