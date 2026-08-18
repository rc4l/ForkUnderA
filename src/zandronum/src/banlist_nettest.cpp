// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The ban list had no tests at all -- not one, for the code that decides who is allowed into a
// server. That was survivable while it did one simple thing to four decimal fields. It is not
// survivable now that an entry can be a v4 pattern or a v6 prefix, because every question ("does this
// address match", "what does this write to disk", "what comes back when it is read again") now has
// two answers and a way to pick the wrong one.
//
// These go through the FILE, not just the in-memory list, because the file is where the two formats
// have to coexist: a v6 rule that cannot survive a save is a ban that changes meaning the next time
// anything else is added.

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"

#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <fstream>

namespace
{

// A ban file in a temp path, written from text and loaded through the real parser.
class BanFile
{
public:
	explicit BanFile( const std::string &contents )
	{
		_path = std::string( ::testing::TempDir( )) + "fua_bantest_" + std::to_string( ++s_counter ) + ".txt";
		std::ofstream out( _path.c_str( ));
		out << contents;
		out.close( );
	}

	~BanFile( ) { std::remove( _path.c_str( )); }

	const char *path( ) const { return _path.c_str( ); }

	std::string readBack( ) const
	{
		std::ifstream in( _path.c_str( ));
		return std::string(( std::istreambuf_iterator<char>( in )), std::istreambuf_iterator<char>( ));
	}

private:
	std::string _path;
	static int s_counter;
};

int BanFile::s_counter = 0;

NETADDRESS_s Addr( const char *text )
{
	NETADDRESS_s address;
	address.LoadFromString( text );
	return address;
}

} // namespace

// --- v4 must not change ---------------------------------------------------------------------------

TEST(BanList, AV4RuleStillMatchesAndAWildcardStillCovers)
{
	BanFile file( "1.2.3.4\n9.9.9.*\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));
	ASSERT_EQ( 2u, list.size( ));

	EXPECT_TRUE( list.isIPInList( Addr( "1.2.3.4" )));
	EXPECT_FALSE( list.isIPInList( Addr( "1.2.3.5" )));
	EXPECT_TRUE( list.isIPInList( Addr( "9.9.9.77" )));
	EXPECT_FALSE( list.isIPInList( Addr( "9.9.8.77" )));
}

// --- v6 ---------------------------------------------------------------------------------------------

TEST(BanList, AV6PrefixMatchesEverythingUnderIt)
{
	BanFile file( "[2001:db8::/32]\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));
	ASSERT_EQ( 1u, list.size( ));

	EXPECT_TRUE( list.isIPInList( Addr( "[2001:db8::1]" )));
	EXPECT_TRUE( list.isIPInList( Addr( "[2001:db8:dead:beef::99]" )));
	EXPECT_FALSE( list.isIPInList( Addr( "[2001:db9::1]" ))) << "one bit outside the prefix is outside the ban";
}

TEST(BanList, AHouseholdSizedPrefixIsTheUsualCase)
{
	// An ISP hands out a /64 and a household is the unit people actually want to ban.
	BanFile file( "[2001:db8:1:2::/64]\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));

	EXPECT_TRUE( list.isIPInList( Addr( "[2001:db8:1:2:aaaa:bbbb:cccc:dddd]" )));
	EXPECT_FALSE( list.isIPInList( Addr( "[2001:db8:1:3::1]" ))) << "the neighbour's /64 must not be banned";
}

TEST(BanList, AV6BanDoesNotBanV4AndTheOtherWayAround)
{
	// The failure this guards against is the one the old code avoided by refusing v6 outright:
	// flattening a v6 address into a v4 rule makes strangers share a ban.
	BanFile file( "[2001:db8::/32]\n5.6.7.8\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));

	EXPECT_FALSE( list.isIPInList( Addr( "5.6.7.9" )));
	EXPECT_TRUE( list.isIPInList( Addr( "5.6.7.8" )));
	EXPECT_FALSE( list.isIPInList( Addr( "[2002::1]" )));
	EXPECT_TRUE( list.isIPInList( Addr( "[2001:db8::5]" )));
}

TEST(BanList, AWildcardV4RuleDoesNotSwallowV6Addresses)
{
	// 0.0.0.* style rules match on empty fields if a v6 address is ever flattened into one, so this is
	// the specific accident being prevented rather than a general tidiness check.
	BanFile file( "*.*.*.*\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));

	EXPECT_TRUE( list.isIPInList( Addr( "1.2.3.4" ))) << "the rule still means every v4 address";
	EXPECT_FALSE( list.isIPInList( Addr( "[2001:db8::1]" ))) << "a v4 wildcard says nothing about v6";
}

// --- the file round trip ----------------------------------------------------------------------------

TEST(BanList, AV6RuleSurvivesBeingWrittenAndReadAgain)
{
	BanFile file( "" );
	std::string message;

	{
		IPList list;
		ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));
		list.addEntry( "[2001:db8:abcd::/48]", "someone", "griefing", message, 0 );
		ASSERT_EQ( 1u, list.size( )) << message;
	}

	// A fresh list, so this is genuinely what the parser makes of what the writer produced.
	IPList reloaded;
	ASSERT_TRUE( reloaded.clearAndLoadFromFile( file.path( )));
	ASSERT_EQ( 1u, reloaded.size( ));
	EXPECT_TRUE( reloaded.isIPInList( Addr( "[2001:db8:abcd:1::9]" )));
	EXPECT_FALSE( reloaded.isIPInList( Addr( "[2001:db8:abce::9]" )));
	EXPECT_STREQ( "someone:griefing", reloaded.getEntry( 0 ).szComment );
}

TEST(BanList, AV6RuleIsWrittenBracketedSoTheParserCanReadItBack)
{
	// The bracket is not decoration: the parser ends an address at ':' and at '/'. Without it the rule
	// on disk would read back as its first group.
	BanFile file( "" );
	std::string message;
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));
	list.addEntry( "2001:db8::/32", NULL, NULL, message, 0 );

	const std::string onDisk = file.readBack( );
	EXPECT_NE( std::string::npos, onDisk.find( "[2001:db8::/32]" )) << "wrote: " << onDisk;
}

TEST(BanList, AV6RuleCanBeRemovedByTheSameTextThatAddedIt)
{
	// A ban that can be added and not deleted leaves editing the file by hand as the only way off it.
	BanFile file( "" );
	std::string message;
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));

	list.addEntry( "[2001:db8::/32]", NULL, NULL, message, 0 );
	ASSERT_EQ( 1u, list.size( ));

	list.removeEntry( "[2001:db8::/32]", message );
	EXPECT_EQ( 0u, list.size( )) << message;

	// And the unbracketed spelling a person would type works too.
	list.addEntry( "2001:db8::/32", NULL, NULL, message, 0 );
	ASSERT_EQ( 1u, list.size( ));
	list.removeEntry( "2001:db8::/32", message );
	EXPECT_EQ( 0u, list.size( ));
}

TEST(BanList, AddingTheSameV6RuleTwiceUpdatesRatherThanDuplicates)
{
	BanFile file( "" );
	std::string message;
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));

	list.addEntry( "[2001:db8::/32]", "first", "reason one", message, 0 );
	list.addEntry( "[2001:db8::/32]", "second", "reason two", message, 0 );

	ASSERT_EQ( 1u, list.size( ));
	EXPECT_STREQ( "second:reason two", list.getEntry( 0 ).szComment );
}

TEST(BanList, MixedV4AndV6EntriesCoexistInOneFile)
{
	BanFile file( "1.2.3.4:someone\n[2001:db8::/32]:someone else\n9.9.9.*\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));
	ASSERT_EQ( 3u, list.size( ));

	EXPECT_TRUE( list.isIPInList( Addr( "1.2.3.4" )));
	EXPECT_TRUE( list.isIPInList( Addr( "[2001:db8::7]" )));
	EXPECT_TRUE( list.isIPInList( Addr( "9.9.9.1" )));
}

TEST(BanList, AV6RuleWithAnExpiryAndAReasonParses)
{
	BanFile file( "[2001:db8::/32]<01/02/2035 15:04>:name:reason\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));
	ASSERT_EQ( 1u, list.size( ));

	EXPECT_TRUE( list.isIPInList( Addr( "[2001:db8::1]" )));
	EXPECT_NE( 0, list.getEntry( 0 ).tExpirationDate );
	EXPECT_STREQ( "name:reason", list.getEntry( 0 ).szComment );
}

// --- malformed input --------------------------------------------------------------------------------

TEST(BanList, ABrokenV6RuleIsDroppedRatherThanGuessedAt)
{
	// A length is mandatory. Reading a bare address as /0 would ban everybody, and that has to be
	// impossible by omission rather than merely unlikely.
	BanFile file( "[2001:db8::]\n[nonsense]\n1.2.3.4\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));

	EXPECT_FALSE( list.isIPInList( Addr( "[2001:db8::1]" ))) << "a prefix with no length must not become a ban";
	EXPECT_TRUE( list.isIPInList( Addr( "1.2.3.4" ))) << "a bad line must not take the good ones with it";
}

TEST(BanList, BanningEveryoneIsPossibleButOnlyWhenSpeltOut)
{
	// "::/0" means every v6 address, and it is accepted BECAUSE the length was written down. The
	// refusal that matters is the accidental one -- a bare "::" is rejected rather than read as /0 --
	// so the difference between one household and the whole internet is never an omission.
	BanFile file( "[::/0]\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));
	ASSERT_EQ( 1u, list.size( ));

	EXPECT_TRUE( list.isIPInList( Addr( "[2001:db8::1]" )));
	EXPECT_TRUE( list.isIPInList( Addr( "[fe80::1]" )));
	EXPECT_FALSE( list.isIPInList( Addr( "1.2.3.4" ))) << "and it is still only a statement about v6";
}

TEST(BanList, AnUnterminatedBracketDoesNotEatTheRestOfTheFile)
{
	BanFile file( "[2001:db8::/32\n1.2.3.4\n" );
	IPList list;
	ASSERT_TRUE( list.clearAndLoadFromFile( file.path( )));

	EXPECT_TRUE( list.isIPInList( Addr( "1.2.3.4" ))) << "the line after a malformed one must still load";
}
