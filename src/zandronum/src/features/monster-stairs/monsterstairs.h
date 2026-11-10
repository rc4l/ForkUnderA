// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Walkers climbing and descending stairs narrower than themselves, chosen by MAPINFO, GAMEINFO or sv_fua_monsterstairs.

#ifndef ZX_MONSTERSTAIRS_H
#define ZX_MONSTERSTAIRS_H

class AActor;

namespace zx { namespace monsterstairs {

// [Doruk-Aksoy] Whether the current level judges a walker's dropoff line by line rather than by its box's whole floor spread.
bool Active();

// [Doruk-Aksoy] After a failed walk, lifts a walker sunk into stairs it overlaps onto the highest step under it; true when it moved.
bool LiftOntoStairs(AActor *actor);

// [Doruk-Aksoy] Clears GAMEINFO's choice before MAPINFO is parsed again, so a wad_reload onto another set does not inherit it.
void ResetGameInfo();

}} // namespace zx::monsterstairs

#endif // ZX_MONSTERSTAIRS_H
