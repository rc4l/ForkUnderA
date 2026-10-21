// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ported from qzandronum@397272811e4f71b168f1949d21369d3e91a7146c. See the header.

#include "features/quake-movement/computation/qtraversal_compute.h"

namespace zx {
namespace quakemove {

namespace {

// Q-Zandronum's threshold: |dot| above this means the pawn is travelling along the wall rather
// than into or away from it.
const float Q_AIRWALLRUN_DOT = 0.75f;

float Min( float a, float b ) { return ( a < b ) ? a : b; }
float Max( float a, float b ) { return ( a > b ) ? a : b; }

} // namespace

float RegenSlideCharge( float tics, float maxTics, float regen )
{
	// Leaving the ground releases the lockout outright -- the magnitude is preserved, only the sign
	// flips, so a player who banked a long lockout does not get it back instantly either.
	if ( tics < 0.0f )
		tics = -tics;
	return Min( maxTics, tics + regen );
}

float DrainSlideCharge( float tics, float maxTics, float regen )
{
	// Standing up converts usable charge into lockout rather than merely stopping regeneration.
	if ( tics > 0.0f )
		tics = -tics;
	return Max( -maxTics, tics - regen );
}

bool CanCrouchSlide( bool hasFlag, bool crouchedEnough, float tics )
{
	return hasFlag && crouchedEnough && ( tics > 0.0f );
}

float RegenSimpleCharge( float tics, float maxTics, float regen )
{
	return Min( maxTics, tics + regen );
}

float SpendCharge( float tics )
{
	return tics - 1.0f;
}

bool HasCharge( float tics )
{
	return tics > 0.0f;
}

bool AirWallRunEngages( float dotAccelWall )
{
	const float magnitude = ( dotAccelWall < 0.0f ) ? -dotAccelWall : dotAccelWall;
	return magnitude > Q_AIRWALLRUN_DOT;
}

bool ShouldEmitEffect( int &effectTics, int interval )
{
	if ( effectTics <= 0 )
	{
		effectTics = interval;
		return true;
	}
	effectTics--;
	return false;
}

} // namespace quakemove
} // namespace zx
