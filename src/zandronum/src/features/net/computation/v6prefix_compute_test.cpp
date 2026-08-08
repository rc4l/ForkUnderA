// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include <cstring>

#include "features/net/computation/v6prefix_compute.h"

using namespace zx;

namespace
{

// A parsed rule, so the tests read as "does this address fall under this rule".
struct Rule
{
	unsigned char prefix[16];
	int bits;
	bool ok;

	explicit Rule( const char *text ) : bits( 0 )
	{
		memset( prefix, 0, sizeof( prefix ));
		ok = ParseV6Prefix( text, prefix, &bits );
	}
};

bool Under( const char *rule, const char *addressGroups )
{
	Rule r( rule );
	if ( !r.ok )
		return false;

	// The address is written as a /128 so the same parser builds both sides.
	char exact[80];
	snprintf( exact, sizeof( exact ), "%s/128", addressGroups );

	Rule a( exact );
	if ( !a.ok )
		return false;

	return V6AddressInPrefix( a.prefix, r.prefix, r.bits );
}

} // namespace

// ---------------------------------------------------------------- prefix notation

TEST(V6Prefix, ParsesAPrefixAndItsLength)
{
	Rule r( "2001:db8::/32" );
	ASSERT_TRUE( r.ok );
	EXPECT_EQ( 32, r.bits );
	EXPECT_EQ( 0x20, r.prefix[0] );
	EXPECT_EQ( 0x01, r.prefix[1] );
	EXPECT_EQ( 0x0d, r.prefix[2] );
	EXPECT_EQ( 0xb8, r.prefix[3] );
}

TEST(V6Prefix, AnAddressInsideTheRangeMatches)
{
	EXPECT_TRUE( Under( "2001:db8::/32", "2001:db8:1234:5678::1" ));
}

TEST(V6Prefix, AnAddressOutsideDoesNot)
{
	EXPECT_FALSE( Under( "2001:db8::/32", "2001:db9::1" ));
}

TEST(V6Prefix, A64IsTheHouseholdAndTheUnitPeopleActuallyBan)
{
	EXPECT_TRUE( Under( "2001:db8:0:1::/64", "2001:db8:0:1:aaaa:bbbb:cccc:dddd" ));
	EXPECT_FALSE( Under( "2001:db8:0:1::/64", "2001:db8:0:2::1" )) << "the neighbour is not banned";
}

TEST(V6Prefix, A128IsExactlyOneAddress)
{
	EXPECT_TRUE( Under( "2001:db8::5/128", "2001:db8::5" ));
	EXPECT_FALSE( Under( "2001:db8::5/128", "2001:db8::6" ));
}

// ---------------------------------------------------------------- asterisks

TEST(V6Prefix, AnAsteriskIsThePrefixPeopleAlreadyKnowHowToWrite)
{
	// Two groups before the star, sixteen bits each.
	Rule r( "2001:db8:*" );
	ASSERT_TRUE( r.ok );
	EXPECT_EQ( 32, r.bits );
}

TEST(V6Prefix, AnAsteriskMatchesTheSameAddressesAsTheEquivalentSlash)
{
	// The property that matters: the two spellings can never disagree, because one becomes the other.
	static const char *const inside[] = {
		"2001:db8::1", "2001:db8:ffff::9", "2001:db8:1:2:3:4:5:6",
	};

	for ( int i = 0; i < 3; ++i )
	{
		EXPECT_EQ( Under( "2001:db8::/32", inside[i] ), Under( "2001:db8:*", inside[i] ))
			<< inside[i];
	}

	EXPECT_EQ( Under( "2001:db8::/32", "2001:db9::1" ), Under( "2001:db8:*", "2001:db9::1" ));
}

TEST(V6Prefix, OneGroupThenAStarIsSixteenBits)
{
	Rule r( "2001:*" );
	ASSERT_TRUE( r.ok );
	EXPECT_EQ( 16, r.bits );
	EXPECT_TRUE( Under( "2001:*", "2001:dead:beef::1" ));
	EXPECT_FALSE( Under( "2001:*", "2002:dead:beef::1" ));
}

TEST(V6Prefix, ABareAsteriskIsRefused)
{
	// It means everybody. Nobody types that on purpose, and a ban list must not accept it quietly.
	EXPECT_FALSE( Rule( "*" ).ok );
}

TEST(V6Prefix, AnAsteriskInTheMiddleIsRefused)
{
	// "2001:*:5" is not a range anybody means, and guessing at one would be inventing a rule.
	EXPECT_FALSE( Rule( "2001:*:5" ).ok );
	EXPECT_FALSE( Rule( "2001:*::1" ).ok );
}

// ---------------------------------------------------------------- refusals

TEST(V6Prefix, AMissingLengthIsRefusedRatherThanGuessed)
{
	// /128 would be the friendly reading and /0 is the one that bans everybody. A format where those
	// differ by an omission is one where the dangerous reading happens by accident.
	EXPECT_FALSE( Rule( "2001:db8::1" ).ok );
}

TEST(V6Prefix, AnOutOfRangeLengthIsRefused)
{
	EXPECT_FALSE( Rule( "2001:db8::/129" ).ok );
	EXPECT_FALSE( Rule( "2001:db8::/-1" ).ok );
}

TEST(V6Prefix, RubbishIsRefused)
{
	EXPECT_FALSE( Rule( "" ).ok );
	EXPECT_FALSE( Rule( "hello/64" ).ok );
	EXPECT_FALSE( Rule( "2001:db8::/abc" ).ok );
	EXPECT_FALSE( Rule( "2001:zzzz::/32" ).ok );
}

TEST(V6Prefix, TwoDoubleColonsAreAmbiguousAndRefused)
{
	EXPECT_FALSE( Rule( "2001::db8::1/64" ).ok );
}

TEST(V6Prefix, AnAddressWithoutADoubleColonMustSpellEveryGroup)
{
	EXPECT_TRUE( Rule( "2001:db8:0:0:0:0:0:1/128" ).ok );
	EXPECT_FALSE( Rule( "2001:db8:0:1/128" ).ok ) << "four groups and no :: is not an address";
}

// ---------------------------------------------------------------- matching edges

TEST(V6Prefix, ABitLengthThatIsNotAWholeNumberOfBytesStillMatches)
{
	// /33 splits a byte, and only the top bit of it counts. Getting the mask backwards here is the
	// bug that makes a ban either far too wide or completely inert.
	unsigned char prefix[16];
	memset( prefix, 0, sizeof( prefix ));
	prefix[4] = 0x80;

	unsigned char inside[16];
	memset( inside, 0, sizeof( inside ));
	inside[4] = 0xff;			// top bit set, rest different

	unsigned char outside[16];
	memset( outside, 0, sizeof( outside ));
	outside[4] = 0x7f;			// top bit clear

	EXPECT_TRUE( V6AddressInPrefix( inside, prefix, 33 ));
	EXPECT_FALSE( V6AddressInPrefix( outside, prefix, 33 ));
}

TEST(V6Prefix, ZeroBitsMatchesEverythingButIsNeverProducedByParsing)
{
	// The function answers honestly for /0; what stops it happening is that nothing can WRITE a /0,
	// which is the bare-asterisk and missing-length refusals above.
	unsigned char a[16], b[16];
	memset( a, 0x11, sizeof( a ));
	memset( b, 0x22, sizeof( b ));

	EXPECT_TRUE( V6AddressInPrefix( a, b, 0 ));

	// And the two ways somebody could reach a /0 by accident are both closed.
	EXPECT_FALSE( Rule( "*" ).ok ) << "a bare asterisk would be everybody";
	EXPECT_FALSE( Rule( "2001:db8::1" ).ok ) << "a missing length must not read as zero";
}

TEST(V6Prefix, AnOutOfRangeLengthMatchesNothing)
{
	unsigned char a[16];
	memset( a, 0, sizeof( a ));

	EXPECT_FALSE( V6AddressInPrefix( a, a, -1 )) << "identical addresses, but the rule is broken";
	EXPECT_FALSE( V6AddressInPrefix( a, a, 129 ));
}

TEST(V6Prefix, NullIsMatchedByNothing)
{
	unsigned char a[16];
	memset( a, 0, sizeof( a ));

	EXPECT_FALSE( V6AddressInPrefix( 0, a, 64 ));
	EXPECT_FALSE( V6AddressInPrefix( a, 0, 64 ));

	int bits = 0;
	EXPECT_FALSE( ParseV6Prefix( 0, a, &bits ));
	EXPECT_FALSE( ParseV6Prefix( "2001:db8::/64", 0, &bits ));
	EXPECT_FALSE( ParseV6Prefix( "2001:db8::/64", a, 0 ));
}
