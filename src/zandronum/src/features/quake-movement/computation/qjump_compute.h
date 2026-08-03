// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ported from qzandronum@397272811e4f71b168f1949d21369d3e91a7146c: the decision logic of
// APlayerPawn::CheckJump and DoubleTapCheck, extracted into pure functions.
//
// Engine-free: no actor, no player_t, no CVAR. The second-jump system is a small state machine plus
// a handful of arming/trigger predicates, and every one of them is a place a subtle mistake turns
// into "double jump sometimes doesn't fire" -- the exact class of bug that is miserable to chase in
// a live game and trivial to pin here.

#ifndef FEATURES_QUAKE_MOVEMENT_QJUMP_COMPUTE_H
#define FEATURES_QUAKE_MOVEMENT_QJUMP_COMPUTE_H

namespace zx {
namespace quakemove {

// Q-Zandronum's second-jump lifecycle. A jump is spent by moving READY -> NOT_AVAILABLE.
enum SecondJumpState
{
	SJ_NOT_AVAILABLE = 0,	// no second jump to spend (used it, or not armed yet)
	SJ_AVAILABLE = 1,		// a second jump exists, but the trigger has not been given
	SJ_READY = 2,			// the trigger fired this tic -- spend it now
};

// The movement flags this unit cares about, mirrored so the pure code needs no engine header.
struct JumpFlags
{
	bool groundSecondJump;	// MV_GROUNDSECONDJUMP: armed from the ground, not only in mid-air
	bool doubleTapJump;		// MV_DOUBLETAPJUMP: a direction double-tap is the trigger
	bool user4Jump;			// MV_USER4JUMP: the user4 button is also a trigger
	bool wallJump;			// MV_WALLJUMP: the second jump requires a nearby wall
	bool wallJumpV2;		// MV_WALLJUMPV2: as above, and it pushes along the wall normal
	bool absoluteSecondJump;// MV_ABSOLUTESECONDJUMP: the second jump sets XY velocity, not adds
	bool edgeJump;			// MV_EDGEJUMP: keep upward velocity when jumping off an edge
};

// What landing does to the second-jump system.
struct GroundedJumpState
{
	int secondJumpsRemaining;
	SecondJumpState state;
	bool resetJumpTics;		// true when jumpTics must be re-armed to JumpDelay
};

// Called every tic the pawn is on the ground. `velz` is only consulted to decide whether the jump
// delay is re-armed, which is what stops a landing player from instantly re-jumping.
GroundedJumpState ComputeGroundedState( int secondJumpAmount, const JumpFlags &flags,
	int secondJumpTics, int jumpTics, int velzUnits );

// Called every tic the pawn is airborne. Returns true when the second jump becomes AVAILABLE.
// The jump button must have been RELEASED since the first jump (unless a different trigger is
// configured) -- otherwise holding jump would spend both jumps on the same press.
bool ComputeAirborneArming( int secondJumpsRemaining, int secondJumpTics, const JumpFlags &flags,
	bool jumpHeld );

// Given an armed second jump, did the player ask for it this tic?
bool ComputeSecondJumpTriggered( const JumpFlags &flags, bool doubleTapFired,
	bool user4JustPressed, bool jumpJustPressed );

// The double-tap detector's per-tic decision. `tapValue` is |forwardmove| + |sidemove|, so it rises
// when a direction is pressed and falls when it is released, without caring which direction.
struct DoubleTapResult
{
	bool fired;
	int lastTapValue;
	int secondJumpTics;
	int lastMoveButtonsBefore;
};

DoubleTapResult ComputeDoubleTap( int tapValue, int lastTapValue, int secondJumpTics,
	int moveButtonsNow, int moveButtonsOld, int lastMoveButtonsBefore, int doubleTapMaxTics );

// The jump delay, in tics. -1 is Zandronum's "no delay" sentinel and is deliberately preserved
// through the doubling below, matching stock behaviour.
int ComputeJumpTics( bool skulltagJumping, bool highJump, bool onSpringPad, int ticRate );

// The two velocity helpers work in RAW fixed-point units (fixed_t::Raw()), not whole map units.
// Passing map units would truncate every fractional JumpZ -- and `Player.JumpZ 8.5` is ordinary in
// mods, so that rounding is a real loss, not a rounding-error quibble.

// The second jump's vertical velocity. It is a floor, not an addition: a player already rising
// faster than the second jump would grant keeps their speed instead of being slowed by using it.
long long ComputeSecondJumpVelZ( long long currentVelZRaw, long long secondJumpZRaw, bool highJump );

// The main jump's vertical velocity. An edge jump keeps whatever upward velocity is already there
// (that is the entire point of the flag); an ordinary jump replaces it.
long long ComputeMainJumpVelZ( long long currentVelZRaw, long long jumpVelZRaw, bool isEdgeJump );

} // namespace quakemove
} // namespace zx

#endif // FEATURES_QUAKE_MOVEMENT_QJUMP_COMPUTE_H
