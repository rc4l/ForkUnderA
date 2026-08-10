// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/core-pk3/computation/corepk3_compute.h"

#include <string>
#include <vector>

using zx::DescribeFoundCores;
using zx::IsCorePk3Name;

// ------------------------------------------------------------------ naming

TEST( CorePk3, RecognisesOurOwn )
{
	EXPECT_TRUE( IsCorePk3Name( "fua_core_v0.2.13.pk3" ));
	EXPECT_TRUE( IsCorePk3Name( "fua_core_dev.pk3" ));
}

TEST( CorePk3, IsCaseInsensitive )
{
	// [rc4l] Windows filesystems are, so a player who typed the name in capitals meant the same file.
	EXPECT_TRUE( IsCorePk3Name( "FUA_CORE_V0.2.13.PK3" ));
	EXPECT_TRUE( IsCorePk3Name( "Fua_Core_v0.2.13.Pk3" ));
}

TEST( CorePk3, RejectsEverythingElse )
{
	EXPECT_FALSE( IsCorePk3Name( "zandronum.pk3" ));
	EXPECT_FALSE( IsCorePk3Name( "brightmaps.pk3" ));
	EXPECT_FALSE( IsCorePk3Name( "skulltag_actors.pk3" ));
	EXPECT_FALSE( IsCorePk3Name( "" ));
}

TEST( CorePk3, RejectsRightPrefixWrongKind )
{
	// A wad is not a pk3, however it is named.
	EXPECT_FALSE( IsCorePk3Name( "fua_core_v0.2.13.wad" ));
	EXPECT_FALSE( IsCorePk3Name( "fua_core_v0.2.13" ));
}

TEST( CorePk3, RejectsAnEmptyKey )
{
	// [rc4l] "fua_core_.pk3" names no build, so treating it as one of ours would have the engine
	// report a core that cannot be matched to anything.
	EXPECT_FALSE( IsCorePk3Name( "fua_core_.pk3" ));
}

TEST( CorePk3, RejectsAPrefixThatIsMerelyContained )
{
	EXPECT_FALSE( IsCorePk3Name( "my_fua_core_v1.pk3" ));
	EXPECT_FALSE( IsCorePk3Name( "fua_cor.pk3" ));
}

TEST( CorePk3, RejectsANameShorterThanThePrefix )
{
	// A pk3 whose whole name is shorter than "fua_core_" runs off the end of the string before the
	// prefix can be ruled out any other way.
	EXPECT_FALSE( IsCorePk3Name( ".pk3" ));
	EXPECT_FALSE( IsCorePk3Name( "a.pk3" ));
}

// ------------------------------------------------------------- the message

TEST( CorePk3, SaysSoWhenThereIsNothingElse )
{
	// Nothing at all and the wrong one are different problems with different fixes, so the empty
	// case gets words rather than a blank line.
	const std::vector<std::string> none;

	EXPECT_EQ( "No other fua_core_*.pk3 is beside the executable either.",
		DescribeFoundCores( "fua_core_v0.2.13.pk3", none ));
}

TEST( CorePk3, NamesTheOneThatIsThere )
{
	std::vector<std::string> found;
	found.push_back( "fua_core_v0.2.12.pk3" );

	const std::string message = DescribeFoundCores( "fua_core_v0.2.13.pk3", found );

	EXPECT_NE( std::string::npos, message.find( "fua_core_v0.2.12.pk3" ));
	EXPECT_NE( std::string::npos, message.find( "ship together" ));
}

TEST( CorePk3, ListsSeveralInOrder )
{
	std::vector<std::string> found;
	found.push_back( "fua_core_v0.2.11.pk3" );
	found.push_back( "fua_core_v0.2.12.pk3" );

	EXPECT_EQ( "Found beside the executable: fua_core_v0.2.11.pk3, fua_core_v0.2.12.pk3.\n"
		"That is a different build's data; the executable and its pk3 ship together.",
		DescribeFoundCores( "fua_core_v0.2.13.pk3", found ));
}

TEST( CorePk3, NeverListsTheOneWeSaidWasMissing )
{
	// [rc4l] The line above this one says it could not be found. Listing it as present would read as
	// a contradiction, so it is filtered even if the scan somehow returns it.
	std::vector<std::string> found;
	found.push_back( "fua_core_v0.2.13.pk3" );

	EXPECT_EQ( "No other fua_core_*.pk3 is beside the executable either.",
		DescribeFoundCores( "fua_core_v0.2.13.pk3", found ));
}

TEST( CorePk3, FiltersTheExpectedOneOutOfALongerList )
{
	std::vector<std::string> found;
	found.push_back( "fua_core_v0.2.13.pk3" );
	found.push_back( "fua_core_v0.2.12.pk3" );

	const std::string message = DescribeFoundCores( "fua_core_v0.2.13.pk3", found );

	EXPECT_NE( std::string::npos, message.find( "fua_core_v0.2.12.pk3" ));
	EXPECT_EQ( std::string::npos, message.find( "fua_core_v0.2.13.pk3" ));
}
