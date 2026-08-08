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
