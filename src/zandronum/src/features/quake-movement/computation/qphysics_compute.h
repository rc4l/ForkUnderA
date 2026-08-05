// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ported from qzandronum@397272811e4f71b168f1949d21369d3e91a7146c: the Quake movement
// arithmetic (APlayerPawn::QFriction / QAcceleration and the surrounding scaling in
// P_MovePlayer_Quake / P_XYMovement), extracted into pure functions.
//
// Engine-free on purpose: no actor, no player_t, no CVAR, no fixed_t. Everything here is float,
// because that is what Quake movement genuinely is -- Q-Zandronum runs this math in FVector3 and
// only converts back to fixed at the boundary. Keeping the conversion OUT of this header is what
// makes it safe under our 48.16 fixed_t (see .claude/skills/fixed64-widening): the glue does every
// fixed<->float crossing explicitly, and none of the deleted Fixed*float operators can be reached
// from here.

#ifndef FEATURES_QUAKE_MOVEMENT_QPHYSICS_COMPUTE_H
#define FEATURES_QUAKE_MOVEMENT_QPHYSICS_COMPUTE_H

namespace zx {
namespace quakemove {

// Quake's tuning constants, verbatim from Q-Zandronum's d_player.h.
const float Q_MAX_GROUND_SPEED = 14.17f;
const float Q_MAX_AIR_SPEED = 12.0f;
const float Q_AIR_ACCELERATION_SCALE = 6.0f;
const float Q_WATER_SPEED_SCALE = 0.6f;
const float Q_WATER_ACCELERATION_SCALE = 6.0f;
const float Q_FLY_ACCELERATION_SCALE = 8.0f;
const float Q_CPM_WISHSPEED = 1.5f;

// The tic rate the per-tic divisions below assume. Mirrors the engine's TICRATE; kept local so this
// header stays engine-free.
const float Q_TICRATE = 35.0f;

// A velocity above this is treated as a wedged actor (Q-Zandronum: "happens when somebody gets
// stuck in a corner, and causes same results as a division by 0") and friction is skipped.
const float Q_RUNAWAY_SPEED = 10000.0f;

struct QVec3
{
	float x, y, z;
};

// Which friction regime the pawn is in this tic. Chosen by the caller from waterlevel / NOGRAVITY /
// onground, because those are engine state; the arithmetic per regime lives here.
enum QFrictionMode
{
	QFRICTION_WATER_OR_FLY,	// drag applies to all three axes, no ground-speed floor
	QFRICTION_GROUND,		// drag applies to X/Y only, and only below the ground-speed limit
	QFRICTION_AIRBORNE,		// no drag at all
};

struct QFrictionResult
{
	// Multiply the velocity components by this. 1.0 means "unchanged".
	float scale;
	// True when the pawn is slow enough to be snapped to a dead stop instead of scaled. The caller
	// must zero the velocity outright -- scaling can never reach exactly zero, so without this a
	// pawn creeps forever at ever-smaller speeds.
	bool stop;
	// True when the Z component is also scaled (water/fly only). On the ground, Z is gravity's.
	bool scaleZ;
};

// One tic of Quake friction. `speed` is the 3D speed for water/fly and is still the 3D speed on the
// ground (Q-Zandronum uses the 3D length for the drop, and the 2D length only for the stop test) --
// the caller passes both so this stays a pure function.
QFrictionResult QFriction( float speed, float speed2D, float groundSpeedLimit, float friction,
	QFrictionMode mode );

// One tic of Quake acceleration, returning the scalar to multiply `wishdir` by. Zero means the pawn
// is already moving at or past `wishspeed` in that direction, which is what makes strafe-jumping
// work: turning changes `currentSpeed` (the dot), so a sideways wish always has headroom left.
float QAccelerationSpeed( float currentSpeed, float wishspeed, float accel );

// The "sticky" velocity cap. Returns the scale to apply to X/Y, or 1.0 when uncapped or under it.
// `cap` of 0 disables the cap entirely. The effective cap is max(cap, speedAtTicStart), so speed
// already earned is kept but no more can be added -- otherwise a cap would yank a rocket-jumping
// player to a halt in mid-air.
float QLocalVelocityCap( float cap, float speedAtTicStart );
float QVelocityCapScale( float speed, float localCap );

// The engine's neutral floor values: a floor carrying these must leave the model untouched.
const int Q_DEFAULT_FLOOR_FRICTION = 59392;	// ORIG_FRICTION
const int Q_DEFAULT_FLOOR_MOVEFACTOR = 2048;

// How far the friction curve may swing either way. See QFloorFrictionForFriction for why an
// unbounded 16th power is not survivable.
const float Q_FLOOR_FRICTION_MIN = 1.0f / 16.0f;
const float Q_FLOOR_FRICTION_MAX = 16.0f;

// The four movement tiers a pawn can be in. These index Player.ForwardMove/SideMove/FootstepsEnabled,
// so the ORDER is part of the DECORATE contract and must not be rearranged: a mod writing
// `Player.ForwardMove 1, 1, 0.5, 0.7` is addressing these positionally.
enum QMoveTier
{
	QTIER_WALK = 0,
	QTIER_RUN = 1,
	QTIER_CROUCH_WALK = 2,
	QTIER_CROUCH_RUN = 3,
};

int QWalkCrouchTier( bool running, bool crouching );

// The extra scaling each tier applies on top of its authored ForwardMove/SideMove entry. Walking is
// half speed and crouch-walking a quarter; running and crouch-running use their entry as authored
// and at half respectively. Verbatim from Q-Zandronum's QCrouchWalkFactor.
float QTierScale( int tier );

// Midpoint between standing (1.0) and the pawn's authored crouch depth. A crouchfactor below this
// counts as "crouching" for the moves that care -- crouch slide engages, air wall run refuses.
float QCrouchHalfWay( float crouchScale );

// Floor friction feeds the two halves of the model differently, and both are deliberately blunted.
// Acceleration takes the 8th root, friction the 16th power, because Quake's own friction is much
// higher than Doom's and an unmodified custom floor value would otherwise dominate the result.
float QFloorFrictionForAccel( int frictionFactor );
float QFloorFrictionForFriction( int frictionValue );

// Rotate (x, y) by `angleDegrees` about the origin. Used to orient the raw forward/side input into
// world space before it becomes a wish direction.
void QVectorRotate( float &x, float &y, float angleDegrees );

float QDotProduct( const QVec3 &a, const QVec3 &b );
float QLength2D( float x, float y );
float QLength3D( const QVec3 &v );
// Unit vector, or {0,0,0} for a zero-length input (rather than a NaN).
QVec3 QMakeUnit( const QVec3 &v );

// CPM's forward-acceleration turn limiter. When the player is already moving and asks to accelerate
// forward at more than `maxAngleRad` off their current heading, the wish direction is clamped to
// exactly that angle -- this is what stops CPM air control from being an instant 180. Returns the
// (possibly rotated) wish direction; `applied` reports whether the clamp actually engaged.
QVec3 QCpmClampForwardWish( const QVec3 &velUnit2D, const QVec3 &wishdir, float maxAngleRad,
	bool &applied );

} // namespace quakemove
} // namespace zx

#endif // FEATURES_QUAKE_MOVEMENT_QPHYSICS_COMPUTE_H
