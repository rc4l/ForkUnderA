// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Which layer decides whether walkers climb narrow stairs, and the dropoff test they then pass or fail.

#ifndef ZX_MONSTERSTAIRS_COMPUTE_H
#define ZX_MONSTERSTAIRS_COMPUTE_H

#include <cstdint>

namespace zx { namespace monsterstairs {

// [Doruk-Aksoy] A MAPINFO map's or GAMEINFO's choice; Unset is 0 so a zero-initialised table already says nothing.
enum class Setting : unsigned char { Unset = 0, On = 1, Off = 2 };

// [Doruk-Aksoy] The setting a `fua_monsterstairs = true|false` key wrote.
Setting ComputeSettingFromBool(bool on);

// [Doruk-Aksoy] The map's own choice, else GAMEINFO's, else the server's sv_fua_monsterstairs.
bool ComputeMonsterStairsEnabled(Setting map, Setting game, bool cvar);

// [Doruk-Aksoy] How far the floor drops across one two-sided line, from its two floor heights in any order; 0 for a side the opening left unbounded.
int64_t ComputeLineFloorDrop(int64_t bottom, int64_t lowfloor);

// [Doruk-Aksoy] Whether a walker must refuse a move for hanging over a dropoff: by the box's whole floor spread, or with `stairs` by its largest single-line drop, which never blocks more than the spread does.
bool ComputeDropoffBlocked(int64_t floorz, int64_t dropoffz, int64_t maxLineDrop, int64_t maxDropOff, bool stairs);

// [Doruk-Aksoy] Whether a walker standing at `z` below the highest floor under its box (`boxFloorz`) should be lifted onto it: only over steps it could climb one line at a time.
bool ComputeStairLift(int64_t z, int64_t boxFloorz, int64_t maxLineDrop, int64_t maxStepHeight);

}} // namespace zx::monsterstairs

#endif // ZX_MONSTERSTAIRS_COMPUTE_H
