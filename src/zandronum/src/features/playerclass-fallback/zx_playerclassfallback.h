// [rc4l] Engine glue for the player-class fallback. The decision itself is pure and lives in
// computation/playerclasspick_compute.*; this only feeds it the engine's PlayerClasses table and
// team rules. See README.md for the bug.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_PLAYERCLASSFALLBACK_H
#define ZX_PLAYERCLASSFALLBACK_H

// Returns a class index to use INSTEAD of rolling a random one, or -1 meaning "nothing to override,
// carry on exactly as before". It answers with an index only when the stored choice is unusable AND
// the loaded mod set NoRandomPlayerClass, so every existing path -- including mods that genuinely
// want random classes -- is bit-for-bit unchanged.
//
// `storedClass` is userinfo's player class (negative or out of range = unusable). `bOnTeam`/`ulTeam`
// come from the player, and gate the fallback to a class that team is allowed to use.
int ZX_PlayerClassFallback( int storedClass, bool bOnTeam, unsigned int ulTeam );

#endif // ZX_PLAYERCLASSFALLBACK_H
