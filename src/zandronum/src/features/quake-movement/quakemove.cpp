// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ported from qzandronum@397272811e4f71b168f1949d21369d3e91a7146c (P_MovePlayer_Quake and
// the Quake friction block in P_XYMovement). See features/quake-movement/README.md.
//
// Everything here is glue: read engine state, cross to float ONCE, call the tested pure functions
// in computation/qphysics_compute.h, cross back. No arithmetic decisions live in this file, and
// every fixed<->float crossing is explicit, which is what keeps it correct under our 48.16 fixed_t
// (Q-Zandronum's original ran this on 32-bit fixed_t with implicit float operators that our strong
// Fixed type deletes outright).

#include "features/quake-movement/quakemove.h"
#include "features/quake-movement/computation/qphysics_compute.h"

#include "actor.h"
#include "d_player.h"
#include "d_event.h"
#include "p_local.h"
#include "g_level.h"
#include "cl_main.h"
#include "cl_demo.h"
#include "network.h"

#include <cmath>

EXTERN_CVAR( Float, cl_spectatormove )

namespace zx {
namespace quakemove {

namespace {

// The powerup/cheat speed multiplier. Q-Zandronum caches this on the actor so their rewritten
// prediction can replay it; we recompute it, because we did not take that prediction rework and a
// cached copy with no one to sync it would just be a second source of truth.
float SpeedFactorFor( player_t *player )
{
	float speedFactor = 1.0f;

	if ((( player->morphTics == 0 ) || ( player->mo->PlayerFlags & PPF_NOMORPHLIMITATIONS )) &&
		( player->mo->Inventory != NULL ))
	{
		speedFactor *= FIXED2FLOAT( player->mo->Inventory->GetSpeedFactor() );
	}

	// [BC] Apply the 25% speed increase power.
	if ( player->cheats & CF_SPEED25 )
		speedFactor *= 1.25f;

	return speedFactor;
}

// How much of the pawn's top speed this tic's input asks for, as a 0..1-ish scalar. Q-Zandronum
// derives this from four ForwardMove/SideMove tiers; ZandroX has two until stage 5 adds the rest,
// so walking vs running is all that is distinguished here.
float CrouchWalkFactor( player_t *player, ticcmd_t *cmd )
{
	QVec3 accel;
	accel.x = FIXED2FLOAT( fixed_t( cmd->ucmd.forwardmove ));
	// The 1.25 matches Q-Zandronum: side input is scaled up before normalising so that a pure
	// strafe reaches the same speed as a pure forward run, which is what makes strafe-jumping
	// symmetric.
	accel.y = -FIXED2FLOAT( fixed_t( cmd->ucmd.sidemove )) * 1.25f;
	accel.z = 0.0f;
	accel = QMakeUnit( accel );
	accel.y /= 1.25f;

	// [Dusk] Let the user move at whatever speed they desire when spectating.
	if ( player->bSpectating )
		return QLength3D( accel );

	// Strife's player can't run when its health is below RunHealth.
	if ( player->mo->health <= player->mo->RunHealth )
		return QLength2D( accel.x * FIXED2FLOAT( fixed_t( 0x1900 )), accel.y * FIXED2FLOAT( fixed_t( 0x1800 )));

	const bool running = (( cmd->ucmd.buttons & BT_SPEED ) != 0 );
	const float forward = FIXED2FLOAT( running ? player->mo->ForwardMove2 : player->mo->ForwardMove1 );
	const float side = FIXED2FLOAT( running ? player->mo->SideMove2 : player->mo->SideMove1 );
	const float tierScale = running ? 1.0f : 0.5f;

	return QLength2D( accel.x * forward * tierScale, accel.y * side * tierScale );
}

// Write a float velocity triple back to the actor, and keep player->velx/vely (the bobbing
// velocity) in step with it.
void StoreVelocity( player_t *player, const QVec3 &vel )
{
	player->mo->velx = FLOAT2FIXED( vel.x );
	player->mo->vely = FLOAT2FIXED( vel.y );
	player->mo->velz = FLOAT2FIXED( vel.z );
}

bool IsWaterOrFlying( const AActor *mo )
{
	return ( mo->waterlevel >= 2 ) || (( mo->flags & MF_NOGRAVITY ) != 0 );
}

} // namespace

bool UsesQuakeMovement( const AActor *mo )
{
	if (( mo == NULL ) || ( mo->player == NULL ))
		return false;
	// A voodoo doll shares the player pointer but is not the body being simulated.
	if ( mo->player->mo != mo )
		return false;
	if ( mo->player->bSpectating )
		return false;
	return static_cast<const APlayerPawn *>( mo )->MvType == MVTYPE_QUAKE;
}

void MovePlayerQuake( player_t *player, ticcmd_t *cmd )
{
	APlayerPawn *const mo = player->mo;

	const float viewAngleDegrees = mo->angle * ( 360.0f / 4294967296.0f );

	int frictionFactor = 0;
	P_GetFriction( mo, &frictionFactor );
	const float floorFriction = QFloorFrictionForAccel( frictionFactor );

	QVec3 vel;
	vel.x = FIXED2FLOAT( mo->velx );
	vel.y = FIXED2FLOAT( mo->vely );
	vel.z = FIXED2FLOAT( mo->velz );

	QVec3 wish;
	wish.x = FIXED2FLOAT( fixed_t( cmd->ucmd.forwardmove ));
	wish.y = -FIXED2FLOAT( fixed_t( cmd->ucmd.sidemove )) * 1.25f;
	wish.z = 0.0f;

	float maxSpeed = FIXED2FLOAT( mo->Speed ) * SpeedFactorFor( player );
	const float moveFactor = CrouchWalkFactor( player, cmd );

	if ( IsWaterOrFlying( mo ))
	{
		// Swimming and flying steer with the view pitch, so the forward input tilts out of the
		// horizontal plane before it is normalised. pitch is a SIGNED BAM in a fixed_t, so it goes
		// out through .Raw() -- FIXED2FLOAT would divide by FRACUNIT and give nonsense here.
		const float pitchDegrees = static_cast<float>( mo->pitch.Raw() ) / static_cast<float>( ANGLE_1 );
		const float pitch = pitchDegrees * 3.14159265358979323846f / 180.0f;
		wish.z = wish.x * std::sin( -pitch );
		wish.x *= std::cos( pitch );

		if (( cmd->ucmd.upmove != 0 ) && !P_IsPlayerTotallyFrozen( player ) &&
			(( player->cheats & CF_FROZEN ) == 0 ))
		{
			wish.z += FIXED2FLOAT( fixed_t( cmd->ucmd.upmove << 4 ));
		}

		QVectorRotate( wish.x, wish.y, viewAngleDegrees );
		wish = QMakeUnit( wish );

		maxSpeed *= Q_MAX_GROUND_SPEED * moveFactor;

		const float wishSpeed = ( mo->waterlevel >= 2 ) ? maxSpeed * Q_WATER_SPEED_SCALE : maxSpeed;
		const float accel = ( mo->waterlevel >= 2 ) ? Q_WATER_ACCELERATION_SCALE : Q_FLY_ACCELERATION_SCALE;

		const float granted = QAccelerationSpeed( QDotProduct( wish, vel ), wishSpeed, accel );
		vel.x += wish.x * granted;
		vel.y += wish.y * granted;
		vel.z += wish.z * granted;

		StoreVelocity( player, vel );
		return;
	}

	QVectorRotate( wish.x, wish.y, viewAngleDegrees );
	wish = QMakeUnit( wish );

	// The cap is resolved BEFORE this tic's acceleration, so speed already earned is kept.
	const float localCap = QLocalVelocityCap( FIXED2FLOAT( mo->VelocityCap ),
		QLength2D( vel.x, vel.y ));

	const bool airborne = ( player->onground == false ) ||
		((( cmd->ucmd.buttons & BT_JUMP ) != 0 ) && ( player->jumpTics <= 0 ));

	if ( airborne )
	{
		maxSpeed *= Q_MAX_AIR_SPEED * moveFactor;
		const float speed2D = QLength2D( vel.x, vel.y );

		if ( mo->mvFlags & MV_CPMAIRCONTROL )
		{
			if (( cmd->ucmd.sidemove != 0 ) && ( cmd->ucmd.forwardmove == 0 ) && ( speed2D >= maxSpeed ))
			{
				// Pure strafe at or past top speed: accelerate against a tiny fixed wish speed, so
				// there is always headroom. This is the CPM strafe-turn.
				const float granted = QAccelerationSpeed( QDotProduct( wish, vel ), Q_CPM_WISHSPEED,
					mo->CpmAirAcceleration * Q_AIR_ACCELERATION_SCALE );
				vel.x += wish.x * granted;
				vel.y += wish.y * granted;
			}
			else
			{
				QVec3 velUnit2D = QMakeUnit( QVec3{ vel.x, vel.y, 0.0f } );
				const float dot = QDotProduct( velUnit2D, wish );

				if (( cmd->ucmd.sidemove == 0 ) && ( speed2D > 0.0f ) && ( dot > 0.0f ))
				{
					bool clamped = false;
					wish = QCpmClampForwardWish( velUnit2D, wish, mo->CpmMaxForwardAngleRad, clamped );
					// Redirect the existing speed along the (possibly clamped) heading rather than
					// adding to it -- CPM forward control turns momentum, it does not create it.
					vel.x = wish.x * speed2D;
					vel.y = wish.y * speed2D;
				}

				const float granted = QAccelerationSpeed( QDotProduct( wish, vel ), maxSpeed,
					FIXED2FLOAT( mo->AirAcceleration ) * Q_AIR_ACCELERATION_SCALE );
				vel.x += wish.x * granted;
				vel.y += wish.y * granted;
			}
		}
		else
		{
			const float granted = QAccelerationSpeed( QDotProduct( wish, vel ), maxSpeed,
				FIXED2FLOAT( mo->AirAcceleration ) * Q_AIR_ACCELERATION_SCALE );
			vel.x += wish.x * granted;
			vel.y += wish.y * granted;
		}
	}
	else
	{
		// Q-Zandronum also skips this branch for one tic after a ThrustThingZ, via a
		// wasJustThrustedZ flag that only exists alongside their thrust-prediction rework. We did
		// not take that rework, so the guard has nothing to hang off and is omitted; the visible
		// difference is that a Z-thrust does not suppress ground acceleration on its landing tic.
		maxSpeed *= Q_MAX_GROUND_SPEED * moveFactor;
		// Dividing the authored acceleration by moveFactor keeps time-to-top-speed constant across
		// walk and run: a walking player has a lower target but reaches it just as promptly.
		const float accel = ( moveFactor > 0.0f )
			? ( mo->GroundAcceleration / moveFactor * floorFriction )
			: ( mo->GroundAcceleration * floorFriction );

		const float granted = QAccelerationSpeed( QDotProduct( wish, vel ), maxSpeed, accel );
		vel.x += wish.x * granted;
		vel.y += wish.y * granted;
	}

	if ( localCap > 0.0f )
	{
		const float scale = QVelocityCapScale( QLength2D( vel.x, vel.y ), localCap );
		vel.x *= scale;
		vel.y *= scale;
	}

	StoreVelocity( player, vel );

	// [BB] Spectators shall stay in their spawn state and don't execute any code pointers.
	if (( CLIENT_PREDICT_IsPredicting() == false ) && player->onground &&
		( player->bSpectating == false ) && ( mo->velx || mo->vely ))
	{
		mo->PlayRunning();
	}
}

bool ApplyQuakeFriction( AActor *mo )
{
	if ( UsesQuakeMovement( mo ) == false )
		return false;

	player_t *const player = mo->player;
	// UsesQuakeMovement already established this is the player's own pawn.
	APlayerPawn *const pawn = static_cast<APlayerPawn *>( mo );

	QVec3 vel;
	vel.x = FIXED2FLOAT( mo->velx );
	vel.y = FIXED2FLOAT( mo->vely );
	vel.z = FIXED2FLOAT( mo->velz );

	const float speed3D = QLength3D( vel );
	const float speed2D = QLength2D( vel.x, vel.y );

	const float maxGroundSpeed = FIXED2FLOAT( pawn->Speed ) * SpeedFactorFor( player ) *
		Q_MAX_GROUND_SPEED * CrouchWalkFactor( player, &player->cmd );

	QFrictionMode mode = QFRICTION_AIRBORNE;
	float friction = 0.0f;
	float limit = 0.0f;

	if ( mo->waterlevel >= 2 )
	{
		mode = QFRICTION_WATER_OR_FLY;
		friction = 2.0f;
	}
	else if ( mo->flags & MF_NOGRAVITY )
	{
		mode = QFRICTION_WATER_OR_FLY;
		friction = 3.0f;
	}
	else if ( player->onground && ( mo->velz <= 0 ))
	{
		mode = QFRICTION_GROUND;
		friction = pawn->GroundFriction * QFloorFrictionForFriction( P_GetFriction( mo, NULL ));
		limit = maxGroundSpeed;
	}

	const QFrictionResult result = QFriction( speed3D, speed2D, limit, friction, mode );

	if ( result.stop )
	{
		mo->velx = 0;
		mo->vely = 0;
		if ( result.scaleZ )
			mo->velz = 0;
		player->velx = 0;
		player->vely = 0;
		return true;
	}

	vel.x *= result.scale;
	vel.y *= result.scale;
	if ( result.scaleZ )
		vel.z *= result.scale;

	// [rc4l] A client does not run friction for OTHER players -- the server already applied it and
	// sent the result, so re-applying it here would double it. See the README's netcode section.
	const bool applyFriction = !( NETWORK_InClientMode() &&
		(( player - players ) != consoleplayer ));

	if ( applyFriction )
	{
		mo->velx = FLOAT2FIXED( vel.x );
		mo->vely = FLOAT2FIXED( vel.y );
		if ( result.scaleZ )
			mo->velz = FLOAT2FIXED( vel.z );
	}

	player->velx = FixedMul( player->velx, FLOAT2FIXED( result.scale ));
	player->vely = FixedMul( player->vely, FLOAT2FIXED( result.scale ));

	return true;
}

} // namespace quakemove
} // namespace zx
