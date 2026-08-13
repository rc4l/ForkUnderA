// [rc4l] Tests for the GAMEMODE table rules. Every line/branch (the coverage gate enforces 100% on
// *_compute.cpp), plus the regression these were extracted for: replaying a boot and then a
// wad_reload onto a different WAD set through the real functions, which is what produced
// "Can't determine if "domination" is cooperative, deathmatch, or team-based." See ../README.md.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/restart-reparse/computation/gamemodetable_compute.h"

#include <gtest/gtest.h>

#include <vector>

// [rc4l] The engine's real flag values, so a renumbering can't quietly invalidate these cases.
// This header is macro-generated enums only -- no engine dependency to link.
#include "gamemode_enums.h"

using namespace zx::gamemodetable;

namespace
{

// [rc4l] Mirrors GAMETYPE_MASK / EARNTYPE_MASK in gamemode.h; the compute layer takes them as
// arguments so it stays free of engine includes.
const unsigned long kGameTypeMask = GMF_COOPERATIVE | GMF_DEATHMATCH | GMF_TEAMGAME;
const unsigned long kEarnTypeMask = GMF_PLAYERSEARNKILLS | GMF_PLAYERSEARNFRAGS |
                                    GMF_PLAYERSEARNPOINTS | GMF_PLAYERSEARNWINS;

// One line of a GAMEMODE block: AddFlag (add == true) or RemoveFlag.
struct Directive
{
	bool add;
	unsigned long bit;
};

// Fold a whole block into a flag word, exactly as the parse loop in gamemode.cpp does.
unsigned long Apply( unsigned long flags, const std::vector<Directive> &block )
{
	for ( size_t i = 0; i < block.size( ); ++i )
		flags = ComputeGameModeFlags( flags, block[i].add, block[i].bit );
	return flags;
}

// The Domination block shipped in wadsrc/static/gamemode.txt.
const std::vector<Directive> kBaseDomination = {
	{ true, GMF_TEAMGAME },
	{ true, GMF_PLAYERSEARNPOINTS },
	{ true, GMF_PLAYERSEARNMEDALS },
	{ true, GMF_PLAYERSONTEAMS },
};

// A mod that repoints Domination at free-for-all deathmatch. Self-consistent on its own: it takes
// the gametype bit away before it adds a different one.
const std::vector<Directive> kModFreeForAll = {
	{ false, GMF_TEAMGAME },
	{ true, GMF_DEATHMATCH },
};

GameModeDefect DefectOf( unsigned long flags )
{
	return ComputeGameModeDefect( true, true, flags, kGameTypeMask, kEarnTypeMask );
}

} // namespace

// ---- ComputeGameModeFlags --------------------------------------------------

TEST( ComputeGameModeFlags, AddSetsAndRemoveClears )
{
	EXPECT_EQ( ComputeGameModeFlags( 0, true, GMF_TEAMGAME ), (unsigned long)GMF_TEAMGAME );
	EXPECT_EQ( ComputeGameModeFlags( GMF_TEAMGAME, false, GMF_TEAMGAME ), 0UL );
}

TEST( ComputeGameModeFlags, LeavesOtherBitsAlone )
{
	const unsigned long start = GMF_TEAMGAME | GMF_PLAYERSONTEAMS;
	EXPECT_EQ( ComputeGameModeFlags( start, false, GMF_TEAMGAME ), (unsigned long)GMF_PLAYERSONTEAMS );
	EXPECT_EQ( ComputeGameModeFlags( start, true, GMF_DEATHMATCH ), start | GMF_DEATHMATCH );
}

TEST( ComputeGameModeFlags, RepeatingADirectiveChangesNothing )
{
	// [rc4l] Why re-parsing the SAME WAD set was always survivable, and only a set change broke:
	// each directive on its own is idempotent, so the damage comes from a directive going missing.
	const unsigned long once = ComputeGameModeFlags( 0, true, GMF_TEAMGAME );
	EXPECT_EQ( ComputeGameModeFlags( once, true, GMF_TEAMGAME ), once );

	const unsigned long gone = ComputeGameModeFlags( once, false, GMF_TEAMGAME );
	EXPECT_EQ( ComputeGameModeFlags( gone, false, GMF_TEAMGAME ), gone );
}

// ---- ComputeHasExactlyOneOf ------------------------------------------------

TEST( ComputeHasExactlyOneOf, TrueForExactlyOneBitOfTheMask )
{
	EXPECT_TRUE( ComputeHasExactlyOneOf( GMF_TEAMGAME, kGameTypeMask ));
	EXPECT_TRUE( ComputeHasExactlyOneOf( GMF_COOPERATIVE, kGameTypeMask ));
}

TEST( ComputeHasExactlyOneOf, FalseForNoneOrMoreThanOne )
{
	EXPECT_FALSE( ComputeHasExactlyOneOf( 0, kGameTypeMask ));
	EXPECT_FALSE( ComputeHasExactlyOneOf( GMF_TEAMGAME | GMF_DEATHMATCH, kGameTypeMask ));
	EXPECT_FALSE( ComputeHasExactlyOneOf( kGameTypeMask, kGameTypeMask ));
}

TEST( ComputeHasExactlyOneOf, IgnoresBitsOutsideTheMask )
{
	// Plenty of other flags set, still exactly one gametype bit.
	const unsigned long flags = GMF_TEAMGAME | GMF_PLAYERSONTEAMS | GMF_PLAYERSEARNMEDALS;
	EXPECT_TRUE( ComputeHasExactlyOneOf( flags, kGameTypeMask ));
	// And a mask that selects nothing can never be satisfied.
	EXPECT_FALSE( ComputeHasExactlyOneOf( flags, 0 ));
}

// ---- ComputeGameModeDefect -------------------------------------------------

TEST( ComputeGameModeDefect, NoneForAWellFormedRow )
{
	EXPECT_EQ( DefectOf( Apply( 0, kBaseDomination )), GameModeDefect::None );
}

TEST( ComputeGameModeDefect, ReportsMissingNamesFirst )
{
	const unsigned long good = Apply( 0, kBaseDomination );
	EXPECT_EQ( ComputeGameModeDefect( false, true, good, kGameTypeMask, kEarnTypeMask ),
	           GameModeDefect::NoName );
	EXPECT_EQ( ComputeGameModeDefect( true, false, good, kGameTypeMask, kEarnTypeMask ),
	           GameModeDefect::NoShortName );
	// A nameless row is reported as nameless even when its flags are broken too.
	EXPECT_EQ( ComputeGameModeDefect( false, false, 0, kGameTypeMask, kEarnTypeMask ),
	           GameModeDefect::NoName );
}

TEST( ComputeGameModeDefect, AmbiguousGameTypeForZeroOrTwoGameTypeBits )
{
	EXPECT_EQ( DefectOf( GMF_PLAYERSEARNPOINTS ), GameModeDefect::AmbiguousGameType );
	EXPECT_EQ( DefectOf( GMF_TEAMGAME | GMF_DEATHMATCH | GMF_PLAYERSEARNPOINTS ),
	           GameModeDefect::AmbiguousGameType );
}

TEST( ComputeGameModeDefect, EarnTypeMustBeExactlyOne )
{
	EXPECT_EQ( DefectOf( GMF_TEAMGAME ), GameModeDefect::NoEarnType );
	EXPECT_EQ( DefectOf( GMF_TEAMGAME | GMF_PLAYERSEARNPOINTS | GMF_PLAYERSEARNFRAGS ),
	           GameModeDefect::MultipleEarnTypes );
	// PLAYERSEARNMEDALS is not an earn TYPE, so it doesn't count towards the pair above.
	EXPECT_EQ( DefectOf( GMF_TEAMGAME | GMF_PLAYERSEARNPOINTS | GMF_PLAYERSEARNMEDALS ),
	           GameModeDefect::None );
}

// ---- Restart regression ----------------------------------------------------
//
// [rc4l] The reported crash, replayed at the table level. Someone joined a server from the browser
// while a mod that repoints Domination was loaded; wad_reload restarted onto the server's set,
// which does not contain that mod.

TEST( RestartReparse, FirstBootWithTheModIsValid )
{
	const unsigned long boot1 = Apply( Apply( 0, kBaseDomination ), kModFreeForAll );
	EXPECT_EQ( boot1 & kGameTypeMask, (unsigned long)GMF_DEATHMATCH );
	EXPECT_EQ( DefectOf( boot1 ), GameModeDefect::None );
}

TEST( RestartReparse, ReparsingOntoTheOldTableStacksTwoGameTypes )
{
	const unsigned long boot1 = Apply( Apply( 0, kBaseDomination ), kModFreeForAll );

	// The new set has only the base lump. Without a reset it lands on top of the mod's edit.
	const unsigned long boot2 = Apply( boot1, kBaseDomination );

	EXPECT_EQ( boot2 & kGameTypeMask, (unsigned long)( GMF_DEATHMATCH | GMF_TEAMGAME ));
	EXPECT_EQ( DefectOf( boot2 ), GameModeDefect::AmbiguousGameType );
}

TEST( RestartReparse, ResettingTheTableFirstRestoresTheBaseRow )
{
	const unsigned long boot1 = Apply( Apply( 0, kBaseDomination ), kModFreeForAll );
	ASSERT_EQ( DefectOf( Apply( boot1, kBaseDomination )), GameModeDefect::AmbiguousGameType );

	// The fix: the parse starts from an empty table, so boot 2 is byte-identical to a cold boot.
	const unsigned long boot2 = Apply( 0, kBaseDomination );
	EXPECT_EQ( boot2, Apply( 0, kBaseDomination ));
	EXPECT_EQ( DefectOf( boot2 ), GameModeDefect::None );
}

TEST( RestartReparse, ReloadingOntoTheSameSetSurvivedEvenUnreset )
{
	// [rc4l] Why this went unnoticed: keep the mod in the new set and the stale bit is corrected
	// again by the mod's own RemoveFlag. Only DROPPING a lump leaves its edit behind.
	const unsigned long boot1 = Apply( Apply( 0, kBaseDomination ), kModFreeForAll );
	const unsigned long boot2 = Apply( Apply( boot1, kBaseDomination ), kModFreeForAll );

	EXPECT_EQ( boot2, boot1 );
	EXPECT_EQ( DefectOf( boot2 ), GameModeDefect::None );
}

TEST( RestartReparse, TheSameLeakBreaksTheEarnTypeCheckToo )
{
	// A mod that pays Domination in frags instead of points, dropped by the reload the same way.
	const std::vector<Directive> modFrags = {
		{ false, GMF_PLAYERSEARNPOINTS },
		{ true, GMF_PLAYERSEARNFRAGS },
	};
	const unsigned long boot1 = Apply( Apply( 0, kBaseDomination ), modFrags );
	ASSERT_EQ( DefectOf( boot1 ), GameModeDefect::None );

	EXPECT_EQ( DefectOf( Apply( boot1, kBaseDomination )), GameModeDefect::MultipleEarnTypes );
	EXPECT_EQ( DefectOf( Apply( 0, kBaseDomination )), GameModeDefect::None );
}
