// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include <string>

#include "features/federated-server-registry/computation/announcefields_compute.h"

using namespace zx;

namespace
{
std::string Hex( size_t length, char c = 'a' ) { return std::string( length, c ); }
}

TEST(AnnounceFields, AnExhaustedStreamIsFalseRatherThanTrue)
{
	// The whole compatibility story.
	EXPECT_FALSE( AnnounceFlagFromByte( -1 ));
	EXPECT_FALSE( AnnounceFlagFromByte( 0 ));
	EXPECT_TRUE( AnnounceFlagFromByte( 1 ));
	EXPECT_TRUE( AnnounceFlagFromByte( 255 ));
}

TEST(AnnounceFields, TheRevisionWidthFollowsWhatIsLeftInThePacket)
{
	// Read a long where a short was written and every field after it is nonsense.
	EXPECT_FALSE( AnnounceUsesLongRevision( 0 ));
	EXPECT_FALSE( AnnounceUsesLongRevision( 3 ));
	EXPECT_TRUE( AnnounceUsesLongRevision( 4 ));
	EXPECT_TRUE( AnnounceUsesLongRevision( 40 ));
}

TEST(AnnounceFields, AProperIdIsGroupable)
{
	EXPECT_TRUE( AnnounceIdIsGroupable( Hex( 64 ).c_str( )));
	EXPECT_TRUE( AnnounceIdIsGroupable( std::string( "0123456789abcdef" ).append( 48, 'f' ).c_str( )));
}

TEST(AnnounceFields, AnAbsentIdIsNotGroupable)
{
	// Every server older than the field. Not grouping them is the correct outcome, not a failure.
	EXPECT_FALSE( AnnounceIdIsGroupable( 0 ));
	EXPECT_FALSE( AnnounceIdIsGroupable( "" ));
}

TEST(AnnounceFields, ATruncatedOrOverlongIdIsRefused)
{
	// A short id is a truncated read and a long one is not ours; either would merge listings that are
	// not one server, which hides somebody's from the browser.
	EXPECT_FALSE( AnnounceIdIsGroupable( Hex( 63 ).c_str( )));
	EXPECT_FALSE( AnnounceIdIsGroupable( Hex( 65 ).c_str( )));
	EXPECT_FALSE( AnnounceIdIsGroupable( Hex( 200 ).c_str( )));
}

TEST(AnnounceFields, AnIdThatIsNotHexIsRefused)
{
	EXPECT_FALSE( AnnounceIdIsGroupable( std::string( 63, 'a' ).append( 1, 'z' ).c_str( )));
	EXPECT_FALSE( AnnounceIdIsGroupable( std::string( 63, 'a' ).append( 1, 'A' ).c_str( )))
		<< "upper case is not what this engine writes, so it is somebody else's value";
	EXPECT_FALSE( AnnounceIdIsGroupable( std::string( 63, 'a' ).append( 1, ' ' ).c_str( )));
}
