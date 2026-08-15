// [rc4l] Pure, engine-free decision for "the stored player class doesn't name a class this mod has:
// now what?". No engine headers, so it is unit-tested off-engine and the coverage gate enforces 100%
// on the matching *_compute.cpp.
//
// Why this exists: `playerclass` is ONE archived cvar shared by every mod, resolved BY NAME against
// whatever is loaded (D_PlayerClassToInt). A name the current mod doesn't define returns -1, and -1
// is also the value meaning "the player chose Random", so an unrecognised leftover silently becomes
// "roll a new class on every spawn". On a mod that registers bot-only classes alongside the real one
// that reads as the player randomly turning into bots after each respawn.
//
// The mod in the report even sets NoRandomPlayerClass, but that GAMEINFO key is only ever read by
// menu code (menudef.cpp, playermenu.cpp, multiplayermenu.cpp, optionmenuitems.h) -- it hides the
// Random entry in the picker and nothing more. The three places that actually roll the dice
// (InitPlayerClasses, G_UpdateSinglePlayerClass, P_SpawnPlayer) never consult it.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_PLAYERCLASSPICK_COMPUTE_H
#define ZX_PLAYERCLASSPICK_COMPUTE_H

#include <vector>

namespace zx { namespace playerclass {

// One entry of the engine's PlayerClasses table, reduced to what the decision needs.
struct ClassCandidate
{
	// PCF_NOMENU: registered but not player-selectable. Mods use it for bot-only classes, which is
	// exactly what must never be handed to a human as a fallback.
	bool hiddenFromMenu;

	// TEAM_IsClassAllowedForTeam for this player's team; true when they aren't on a team.
	bool allowedForTeam;
};

enum class PickKind
{
	Stored,         // the stored choice is usable; use `index`
	FirstEligible,  // stored choice unusable and this mod forbids random classes; use `index`
	RollRandom,     // stored choice unusable and random is allowed; caller rolls exactly as before
};

struct Pick
{
	PickKind kind;
	int index;      // meaningful for Stored and FirstEligible; -1 for RollRandom
};

// `storedClass` is userinfo's player class: a real index, or negative for "Random or unresolvable".
// An index past the end counts as unusable too -- that is a stale choice left by a mod with more
// classes than the current one.
//
// RollRandom is returned whenever random is permitted, so Hexen-style mods that genuinely want a
// random class keep behaving exactly as they do today: this never takes randomness away, it only
// declines to invent it where the mod said not to.
Pick ComputePlayerClassPick( int storedClass, bool forbidRandom,
                             const std::vector<ClassCandidate> &candidates );

}} // namespace zx::playerclass

#endif // ZX_PLAYERCLASSPICK_COMPUTE_H
