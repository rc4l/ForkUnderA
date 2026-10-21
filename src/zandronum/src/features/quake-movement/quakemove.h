// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Quake movement engine glue. See features/quake-movement/README.md.
//
// This header is the whole surface the engine sees: p_user.cpp dispatches into MovePlayerQuake,
// and p_mobj.cpp asks UsesQuakeMovement / applies ApplyQuakeFriction inside P_XYMovement. All the
// arithmetic lives in computation/qphysics_compute.h, which is engine-free and unit-tested.

#ifndef FEATURES_QUAKE_MOVEMENT_QUAKEMOVE_H
#define FEATURES_QUAKE_MOVEMENT_QUAKEMOVE_H

struct player_t;
struct ticcmd_t;
class AActor;

namespace zx {
namespace quakemove {

// True when this actor is a player pawn simulating under MVTYPE_QUAKE. Voodoo dolls and spectators
// are excluded: a voodoo doll is not the player's body, and spectator movement is a free-fly camera
// rather than simulated physics (Q-Zandronum makes the same two exceptions).
bool UsesQuakeMovement( const AActor *mo );

// The Quake horizontal/vertical acceleration model for one tic. Called from P_MovePlayer in place
// of the Doom body.
//
// Returns true when this tic's jump input has already been consumed and the caller must NOT run the
// jump block: swimming/flying steer with the jump key, and a wall climb is driven by holding it, so
// letting the jump fire as well would launch the player off the wall they are climbing.
bool MovePlayerQuake( player_t *player, ticcmd_t *cmd );

// The Quake friction model for one tic, applied from P_XYMovement AFTER the move (which is where
// Quake friction belongs, and why the server has to send pre-friction velocity -- see the README).
// Returns true when it handled friction, so the caller skips the Doom friction path entirely.
bool ApplyQuakeFriction( AActor *mo );

// The Quake jump model for one tic: the second-jump state machine, wall jump, double-tap dash and
// edge jump. Replaces the stock jump block for Quake-movement pawns. Returns true when it handled
// the jump, so the caller skips the Doom jump path.
bool CheckJumpQuake( player_t *player, ticcmd_t *cmd );

} // namespace quakemove
} // namespace zx

#endif // FEATURES_QUAKE_MOVEMENT_QUAKEMOVE_H
