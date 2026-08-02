// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ported from qzandronum@397272811e4f71b168f1949d21369d3e91a7146c. See the header for why
// this is float-only and engine-free.

#include "features/quake-movement/computation/qphysics_compute.h"

#include <cmath>

namespace zx {
namespace quakemove {

namespace {

const float Q_PI = 3.14159265358979323846f;

// Below this 2D speed a grounded pawn is snapped to rest. Q-Zandronum uses 1.0 on the ground and
// 0.5 in water/air, and the difference matters: a swimmer drifting to a halt at 0.9 would never
// stop if the ground threshold were used, because water drag scales rather than subtracts.
const float Q_STOP_SPEED_GROUND = 1.0f;
const float Q_STOP_SPEED_WATER = 0.5f;

} // namespace

QFrictionResult QFriction( float speed, float speed2D, float groundSpeedLimit, float friction,
	QFrictionMode mode )
{
	QFrictionResult result;
	result.scale = 1.0f;
	result.stop = false;
	result.scaleZ = ( mode == QFRICTION_WATER_OR_FLY );

	// A wedged actor produces absurd speeds; scaling them is the same class of failure as dividing
	// by zero, so leave the velocity alone and let the collision code sort it out.
	if ( speed > Q_RUNAWAY_SPEED )
		return result;

	if ( mode == QFRICTION_WATER_OR_FLY )
	{
		if ( speed < Q_STOP_SPEED_WATER )
		{
			result.stop = true;
			return result;
		}
	}
	else if ( speed2D < Q_STOP_SPEED_GROUND )
	{
		result.stop = true;
		return result;
	}

	// Zero speed past the stop tests can only mean a runaway-guarded NaN or a mode that skipped the
	// test; guard the division rather than emitting an infinity.
	if ( speed <= 0.0f )
		return result;

	float drop = 0.0f;
	if ( mode == QFRICTION_WATER_OR_FLY )
	{
		drop = speed * friction / Q_TICRATE;
	}
	else if ( mode == QFRICTION_GROUND )
	{
		// Below the ground-speed limit the drop is a flat friction-per-tic; above it the drop grows
		// with speed, which is what makes excess speed bleed off fast but leaves normal running
		// speed stable.
		const float control = ( speed < groundSpeedLimit ) ? friction : speed;
		drop = control * friction / Q_TICRATE;
	}
	// QFRICTION_AIRBORNE: drop stays 0 -- no air friction, which is the whole point of the model.

	const float remaining = speed - drop;
	result.scale = ( remaining > 0.0f ? remaining : 0.0f ) / speed;
	return result;
}

float QAccelerationSpeed( float currentSpeed, float wishspeed, float accel )
{
	const float addspeed = wishspeed - currentSpeed;
	if ( addspeed <= 0.0f )
		return 0.0f;

	const float accelerationspeed = accel * wishspeed / Q_TICRATE;
	return ( accelerationspeed < addspeed ) ? accelerationspeed : addspeed;
}

float QLocalVelocityCap( float cap, float speedAtTicStart )
{
	if ( cap <= 0.0f )
		return 0.0f;
	return ( cap > speedAtTicStart ) ? cap : speedAtTicStart;
}

float QVelocityCapScale( float speed, float localCap )
{
	if ( localCap <= 0.0f || speed <= localCap || speed <= 0.0f )
		return 1.0f;
	return localCap / speed;
}

float QFloorFrictionForAccel( int frictionFactor )
{
	// 2048 is the default floor move factor, so a default floor yields exactly 1.0.
	const float ratio = static_cast<float>( frictionFactor ) / 2048.0f;
	if ( ratio <= 0.0f )
		return 0.0f;
	return std::pow( ratio, 0.125f );
}

float QFloorFrictionForFriction( int frictionValue )
{
	// 59392 is the default floor friction, so a default floor yields exactly 1.0. Dividing this way
	// round means an icier floor (lower friction value) produces a ratio above 1, and the 16th
	// power then turns a small authored change into a large one -- which is the intent, because
	// Quake friction is otherwise far too dominant for a custom floor to be felt at all.
	if ( frictionValue == 0 )
		return 0.0f;
	const float ratio = 59392.0f / static_cast<float>( frictionValue );
	return std::pow( ratio, 16.0f );
}

void QVectorRotate( float &x, float &y, float angleDegrees )
{
	const float radians = angleDegrees * Q_PI / 180.0f;
	const float oldX = x, oldY = y;
	const float cosine = std::cos( radians ), sine = std::sin( radians );
	x = cosine * oldX - sine * oldY;
	y = sine * oldX + cosine * oldY;
}

float QDotProduct( const QVec3 &a, const QVec3 &b )
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

float QLength2D( float x, float y )
{
	return std::sqrt( x * x + y * y );
}

float QLength3D( const QVec3 &v )
{
	return std::sqrt( v.x * v.x + v.y * v.y + v.z * v.z );
}

QVec3 QMakeUnit( const QVec3 &v )
{
	const float length = QLength3D( v );
	if ( length <= 0.0f )
	{
		QVec3 zero = { 0.0f, 0.0f, 0.0f };
		return zero;
	}
	QVec3 unit = { v.x / length, v.y / length, v.z / length };
	return unit;
}

QVec3 QCpmClampForwardWish( const QVec3 &velUnit2D, const QVec3 &wishdir, float maxAngleRad,
	bool &applied )
{
	applied = false;

	const float dot = QDotProduct( velUnit2D, wishdir );
	// Already within the cone (or facing away entirely -- the caller only reaches here with dot > 0).
	if ( dot >= std::cos( maxAngleRad ) )
		return wishdir;

	// Rotate the CURRENT heading by exactly the maximum angle, in whichever direction the player
	// asked for. Using the cross product's sign picks the shorter turn, so holding hard left and
	// hard right are mirror images rather than one of them snapping the long way round.
	const float cross = velUnit2D.x * wishdir.y - velUnit2D.y * wishdir.x;
	const float radians = ( cross > 0.0f ) ? maxAngleRad : -maxAngleRad;

	const float cosine = std::cos( radians ), sine = std::sin( radians );
	QVec3 clamped;
	clamped.x = velUnit2D.x * cosine - velUnit2D.y * sine;
	clamped.y = velUnit2D.x * sine + velUnit2D.y * cosine;
	clamped.z = wishdir.z;

	applied = true;
	return clamped;
}

} // namespace quakemove
} // namespace zx
