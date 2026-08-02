// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ported from qzandronum@397272811e4f71b168f1949d21369d3e91a7146c. See the header.

#include "features/quake-movement/computation/qjump_compute.h"

namespace zx {
namespace quakemove {

GroundedJumpState ComputeGroundedState( int secondJumpAmount, const JumpFlags &flags,
	int secondJumpTics, int jumpTics, int velzUnits )
{
	GroundedJumpState out;
	out.secondJumpsRemaining = secondJumpAmount;

	// A ground-armed second jump still respects its own cooldown, or landing would refund a jump
	// the player has not waited out.
	out.state = ( flags.groundSecondJump && ( secondJumpTics <= 0 ))
		? SJ_AVAILABLE
		: SJ_NOT_AVAILABLE;

	// Re-arm the delay when the previous jump left it at the "no delay" sentinel, or when the pawn
	// arrived with real downward speed (i.e. actually landed rather than merely touching ground).
	out.resetJumpTics = ( jumpTics < 0 ) || ( velzUnits < -8 );

	return out;
}

bool ComputeAirborneArming( int secondJumpsRemaining, int secondJumpTics, const JumpFlags &flags,
	bool jumpHeld )
{
	// A remaining count of 0 means spent; a NEGATIVE count means unlimited, which is why this is
	// "!= 0" rather than "> 0".
	if ( secondJumpsRemaining == 0 )
		return false;
	if ( secondJumpTics > 0 )
		return false;

	// With a dedicated trigger (double-tap or user4), the jump button's state is irrelevant. With
	// the plain jump-button trigger it must have been released first, or one long press would spend
	// both jumps on the tic after take-off.
	if ( flags.doubleTapJump || flags.user4Jump )
		return true;
	return ( jumpHeld == false );
}

bool ComputeSecondJumpTriggered( const JumpFlags &flags, bool doubleTapFired,
	bool user4JustPressed, bool jumpJustPressed )
{
	if ( flags.doubleTapJump )
		return doubleTapFired || user4JustPressed;

	// Note the asymmetry, which is Q-Zandronum's: when user4 is the configured trigger, the jump
	// button stops triggering a second jump at all.
	const bool jumpTriggers = ( flags.user4Jump == false ) && jumpJustPressed;
	return jumpTriggers || user4JustPressed;
}

DoubleTapResult ComputeDoubleTap( int tapValue, int lastTapValue, int secondJumpTics,
	int moveButtonsNow, int moveButtonsOld, int lastMoveButtonsBefore, int doubleTapMaxTics )
{
	DoubleTapResult out;
	out.fired = false;
	out.lastTapValue = lastTapValue;
	out.secondJumpTics = secondJumpTics;
	out.lastMoveButtonsBefore = lastMoveButtonsBefore;

	if ( tapValue > lastTapValue )
	{
		// A direction was just pressed.
		out.lastTapValue = tapValue;

		// A NEGATIVE secondJumpTics is the double-tap window counting down; the tap only counts if
		// it is the same direction set that was released, so tapping forward then back is not a
		// double-tap.
		if (( secondJumpTics < 0 ) && ( moveButtonsNow == lastMoveButtonsBefore ))
		{
			out.lastMoveButtonsBefore = 0;
			out.fired = true;
		}
		else
		{
			// Open (or restart) the window.
			out.secondJumpTics = -doubleTapMaxTics;
		}
	}
	else if ( tapValue < lastTapValue )
	{
		// A direction was just released -- remember which, so the re-press can be matched.
		out.lastMoveButtonsBefore = moveButtonsOld;
		out.lastTapValue = tapValue;
	}

	return out;
}

int ComputeJumpTics( bool skulltagJumping, bool highJump, bool onSpringPad, int ticRate )
{
	int tics = skulltagJumping ? ( 18 * ticRate / 35 ) : -1;

	// [BC] Increase jump delay if the player has the high jump power.
	if ( highJump )
		tics *= 2;

	// [BC] Remove jump delay if the player is on a spring pad. Checked last so it wins over the
	// high-jump doubling above.
	if ( onSpringPad )
		tics = 0;

	return tics;
}

int ComputeSecondJumpVelZ( int currentVelZ, int secondJumpZ, bool highJump )
{
	int jumpVelZ = secondJumpZ;
	if ( highJump )
		jumpVelZ *= 2;

	return ( currentVelZ > jumpVelZ ) ? currentVelZ : jumpVelZ;
}

int ComputeMainJumpVelZ( int currentVelZ, int jumpVelZ, bool isEdgeJump )
{
	const int base = isEdgeJump ? (( currentVelZ > 0 ) ? currentVelZ : 0 ) : 0;
	return base + jumpVelZ;
}

} // namespace quakemove
} // namespace zx
