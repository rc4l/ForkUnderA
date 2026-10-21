// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ported from qzandronum@397272811e4f71b168f1949d21369d3e91a7146c: the charge/regen
// bookkeeping behind crouch slide, wall climb and air wall run, plus the air-wall-run surface test.
//
// Engine-free. The charge arithmetic is the whole reason this file exists: Q-Zandronum encodes two
// different states in ONE signed counter, and the sign flips are easy to get subtly wrong in a way
// that only shows up as "my slide sometimes doesn't work".

#ifndef FEATURES_QUAKE_MOVEMENT_QTRAVERSAL_COMPUTE_H
#define FEATURES_QUAKE_MOVEMENT_QTRAVERSAL_COMPUTE_H

namespace zx {
namespace quakemove {

// A crouch-slide charge is a SIGNED counter with two meanings:
//   > 0  usable charge, in tics of slide remaining
//   < 0  locked out; the magnitude is how much has been "banked back" while standing
// The sign is what enforces "you must leave the ground to get your slide back" -- standing up does
// not simply refill it, it pushes the counter negative and the airborne path is what flips it home.

// Airborne: a locked-out charge is released (sign flipped positive) and then regenerates, capped.
float RegenSlideCharge( float tics, float maxTics, float regen );

// Grounded and NOT crouched: a usable charge is locked out (sign flipped negative) and then drains
// further, floored at -maxTics.
float DrainSlideCharge( float tics, float maxTics, float regen );

// True when a crouch slide may run this tic. Requires the flag, a crouch deep enough to count, and
// actual charge left -- a locked-out (negative) charge is not charge.
bool CanCrouchSlide( bool hasFlag, bool crouchedEnough, float tics );

// The simple charge model shared by wall climb and air wall run: regenerate toward maxTics while
// not in use, spend one tic per tic while in use. No sign trickery -- these lock out at zero.
float RegenSimpleCharge( float tics, float maxTics, float regen );
float SpendCharge( float tics );
bool HasCharge( float tics );

// Air wall run engages only when the pawn is moving roughly ALONG the wall rather than into it.
// `dot` is between the pawn's acceleration direction and the wall's direction vector; the
// magnitude is what matters, since running the wall in either direction is equally valid.
bool AirWallRunEngages( float dotAccelWall );

// The effect-actor interval counter: emit when it reaches zero, then reload. Returns whether to
// emit this tic and writes back the next counter value.
bool ShouldEmitEffect( int &effectTics, int interval );

} // namespace quakemove
} // namespace zx

#endif // FEATURES_QUAKE_MOVEMENT_QTRAVERSAL_COMPUTE_H
