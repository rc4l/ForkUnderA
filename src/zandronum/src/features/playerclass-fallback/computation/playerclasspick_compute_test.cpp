// [rc4l] Tests for the player-class fallback decision. Every line/branch (the coverage gate enforces
// 100% on *_compute.cpp), plus the reported regression: a mod whose only selectable class sits
// alongside three bot-only ones, where an unresolvable `playerclass` used to mean "re-roll on every
// respawn" and the player kept turning into bots. See ../README.md.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/playerclass-fallback/computation/playerclasspick_compute.h"

#include <gtest/gtest.h>

using namespace zx::playerclass;

namespace
{

ClassCandidate Selectable( ) { ClassCandidate c = { false, true }; return c; }
ClassCandidate BotOnly( )    { ClassCandidate c = { true, true };  return c; }
ClassCandidate TeamLocked( ) { ClassCandidate c = { false, false }; return c; }

// [rc4l] The reported mod: MAPINFO registers Street_Ninja, KEYCONF adds three `nomenu` bot classes.
std::vector<ClassCandidate> StreetNinjaMod( )
{
	return { Selectable( ), BotOnly( ), BotOnly( ), BotOnly( ) };
}

} // namespace

// ---- ComputePlayerClassPick ------------------------------------------------

TEST( PlayerClassPick, KeepsAUsableStoredChoice )
{
	const Pick pick = ComputePlayerClassPick( 2, true, StreetNinjaMod( ));
	EXPECT_EQ( pick.kind, PickKind::Stored );
	EXPECT_EQ( pick.index, 2 );
}

TEST( PlayerClassPick, AnExplicitChoiceOfAHiddenClassIsStillHonoured )
{
	// [rc4l] ACS SetPlayerClass can legitimately put a player in a bot class; only the FALLBACK
	// avoids them.
	const Pick pick = ComputePlayerClassPick( 1, true, StreetNinjaMod( ));
	EXPECT_EQ( pick.kind, PickKind::Stored );
	EXPECT_EQ( pick.index, 1 );
}

TEST( PlayerClassPick, RandomStaysRandomWhenTheModAllowsIt )
{
	// [rc4l] Hexen and friends must be untouched: this never takes randomness away.
	const Pick pick = ComputePlayerClassPick( -1, false, StreetNinjaMod( ));
	EXPECT_EQ( pick.kind, PickKind::RollRandom );
	EXPECT_EQ( pick.index, -1 );
}

TEST( PlayerClassPick, NoCandidatesLeavesTheCallerAlone )
{
	const Pick pick = ComputePlayerClassPick( -1, true, {} );
	EXPECT_EQ( pick.kind, PickKind::RollRandom );
}

TEST( PlayerClassPick, SkipsBotOnlyClassesWhenFallingBack )
{
	// Registration order reversed: the bot classes come first, so a naive "use index 0" would hand
	// the player a bot.
	const std::vector<ClassCandidate> candidates = { BotOnly( ), BotOnly( ), Selectable( ) };
	const Pick pick = ComputePlayerClassPick( -1, true, candidates );
	EXPECT_EQ( pick.kind, PickKind::FirstEligible );
	EXPECT_EQ( pick.index, 2 );
}

TEST( PlayerClassPick, SkipsClassesThisTeamMayNotUse )
{
	const std::vector<ClassCandidate> candidates = { TeamLocked( ), Selectable( ) };
	const Pick pick = ComputePlayerClassPick( -1, true, candidates );
	EXPECT_EQ( pick.kind, PickKind::FirstEligible );
	EXPECT_EQ( pick.index, 1 );
}

TEST( PlayerClassPick, RollsWhenNothingIsEligible )
{
	// Forcing a class the team forbids would be worse than the roll the engine already did.
	const std::vector<ClassCandidate> candidates = { BotOnly( ), TeamLocked( ) };
	const Pick pick = ComputePlayerClassPick( -1, true, candidates );
	EXPECT_EQ( pick.kind, PickKind::RollRandom );
}

TEST( PlayerClassPick, AStaleIndexPastTheEndCountsAsUnusable )
{
	// [rc4l] The other way a leftover choice arrives: an INDEX saved by a mod with more classes.
	const Pick pick = ComputePlayerClassPick( 9, true, StreetNinjaMod( ));
	EXPECT_EQ( pick.kind, PickKind::FirstEligible );
	EXPECT_EQ( pick.index, 0 );
}

// ---- The reported regression ----------------------------------------------

TEST( PlayerClassRegression, UnresolvedNameNoLongerReRollsIntoBotClasses )
{
	// What used to happen: `playerclass` held "Fighter" (ZDoom's default, absent from this mod), so
	// D_PlayerClassToInt returned -1, which every spawn read as "roll one of the four" -- three of
	// which are bots. The mod's own NoRandomPlayerClass was ignored because only menu code read it.
	const Pick pick = ComputePlayerClassPick( -1, /*forbidRandom=*/true, StreetNinjaMod( ));

	EXPECT_EQ( pick.kind, PickKind::FirstEligible );
	EXPECT_EQ( pick.index, 0 ) << "must land on Street_Ninja, never a bot class";

	// And the same table with random permitted is untouched, so this is scoped to mods that asked.
	EXPECT_EQ( ComputePlayerClassPick( -1, false, StreetNinjaMod( )).kind, PickKind::RollRandom );
}

