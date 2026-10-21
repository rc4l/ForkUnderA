// [rc4l] Pure, engine-free rules for the GAMEMODE lump's game-mode table, extracted out of
// gamemode.cpp so they can be unit-tested off-engine (the coverage gate enforces 100% on the
// matching *_compute.cpp). Two rules live here: how a single AddFlag/RemoveFlag directive folds
// into a mode's flag word, and whether the resulting row is self-consistent.
//
// Worth owning because the consistency check is a hard I_Error -- it kills the engine mid-restart,
// after teardown, where nothing can roll back. See ../README.md for the restart bug it caught.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_GAMEMODETABLE_COMPUTE_H
#define ZX_GAMEMODETABLE_COMPUTE_H

namespace zx { namespace gamemodetable {

// One "AddFlag X" / "RemoveFlag X" directive folded into a game mode's flag word. Note what it is
// NOT: no GAMEMODE lump ever states a mode's flags outright, every directive mutates whatever is
// already there. So a parse only means what it reads if the table it starts from is empty.
unsigned long ComputeGameModeFlags( unsigned long flags, bool add, unsigned long bit );

// True when exactly one bit of `mask` is set in `flags`. Zero bits and two-or-more both return
// false: for the gametype bits either one is equally unusable, which is why the engine's message
// there is "can't determine", not "too many".
bool ComputeHasExactlyOneOf( unsigned long flags, unsigned long mask );

// What (if anything) makes one row of the table unusable. Ordered: the first problem found is the
// one reported, matching the order the engine checks and reports them in.
enum class GameModeDefect
{
	None,
	NoName,
	NoShortName,
	AmbiguousGameType,   // not exactly one of cooperative / deathmatch / teamgame
	NoEarnType,          // no way for players to earn kills, frags, points or wins
	MultipleEarnTypes,
};

// `gameTypeMask` / `earnTypeMask` are the engine's GAMETYPE_MASK / EARNTYPE_MASK (gamemode.h);
// they are passed in rather than hardcoded to keep this header free of engine includes.
GameModeDefect ComputeGameModeDefect( bool hasName, bool hasShortName, unsigned long flags,
                                      unsigned long gameTypeMask, unsigned long earnTypeMask );

}} // namespace zx::gamemodetable

#endif // ZX_GAMEMODETABLE_COMPUTE_H
