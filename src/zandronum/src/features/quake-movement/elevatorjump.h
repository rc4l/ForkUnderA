// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ported from qzandronum@397272811e4f71b168f1949d21369d3e91a7146c (P_CheckForElevatorJump
// and sector_t::GetFloorMovingSpeed). See features/quake-movement/README.md.
//
// +ELEVATORJUMP: jumping off a rising floor adds the floor's own speed to the jump, so a lift
// carrying you upward launches you instead of "eating" the jump. Without it the floor rises into
// the player during the same tic the jump starts and silently cancels most of the launch.

#ifndef FEATURES_QUAKE_MOVEMENT_ELEVATORJUMP_H
#define FEATURES_QUAKE_MOVEMENT_ELEVATORJUMP_H

#include "doomtype.h"

class AActor;
struct sector_t;

namespace zx {
namespace quakemove {

// The signed vertical speed the sector's floor mover is currently applying, in fixed_t units per
// tic. Positive is upward; 0 when nothing is moving it. Only the mover kinds that can actually
// carry a standing player are consulted.
fixed_t FloorMovingSpeed( sector_t *sector );
fixed_t CeilingMovingSpeed( sector_t *sector );

// Add the carrying floor's speed to the actor's velz, if it has +ELEVATORJUMP and is standing on
// something that is rising. Called right after a jump sets velz.
void ApplyElevatorJump( AActor *mo );

} // namespace quakemove
} // namespace zx

#endif // FEATURES_QUAKE_MOVEMENT_ELEVATORJUMP_H
