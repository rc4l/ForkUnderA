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
#include "features/quake-movement/computation/qjump_compute.h"
#include "features/quake-movement/computation/qtraversal_compute.h"
#include "features/quake-movement/elevatorjump.h"

#include "actor.h"
#include "d_player.h"
#include "d_event.h"
#include "p_local.h"
#include "p_trace.h"
#include "r_defs.h"
#include "s_sound.h"
#include "tables.h"
#include "g_level.h"
#include "sv_commands.h"
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

// Which of the four move tiers this tic's input represents. BT_SPEED means "is running" for a Quake
// pawn (see G_BuildTiccmd), and the crouch test is the pawn's own authored depth rather than a
// fixed constant, so a shallow-crouching class still registers as crouched.
int MoveTierOf( player_t *player, ticcmd_t *cmd )
{
	const bool running = (( cmd->ucmd.buttons & BT_SPEED ) != 0 );
	const float halfWay = QCrouchHalfWay( FIXED2FLOAT( player->mo->CrouchScale ));
	const bool crouching = ( FIXED2FLOAT( player->crouchfactor ) < halfWay );
	return QWalkCrouchTier( running, crouching );
}

// How much of the pawn's top speed this tic's input asks for, as a 0..1-ish scalar, from the
// authored ForwardMove/SideMove entry for the current tier.
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

	const int tier = MoveTierOf( player, cmd );
	const APlayerPawn *const mo = player->mo;

	fixed_t forwardEntry, sideEntry;
	switch ( tier )
	{
	case QTIER_RUN:			forwardEntry = mo->ForwardMove2; sideEntry = mo->SideMove2; break;
	case QTIER_CROUCH_WALK:	forwardEntry = mo->ForwardMove3; sideEntry = mo->SideMove3; break;
	case QTIER_CROUCH_RUN:	forwardEntry = mo->ForwardMove4; sideEntry = mo->SideMove4; break;
	default:				forwardEntry = mo->ForwardMove1; sideEntry = mo->SideMove1; break;
	}

	const float tierScale = QTierScale( tier );
	return QLength2D( accel.x * FIXED2FLOAT( forwardEntry ) * tierScale,
		accel.y * FIXED2FLOAT( sideEntry ) * tierScale );
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

// [rc4l] The Quake friction branch returns early out of P_XYMovement, which would otherwise skip
// killough's "come to rest" block -- so a Quake pawn that stopped kept running on the spot forever.
// Q-Zandronum copies the same block into their branch for the same reason.
void StopAndIdleIfAtRest( APlayerPawn *mo )
{
	// STOPSPEED is p_mobj.cpp-local; 0x1000 is its value and the threshold this mirrors.
	const fixed_t stopSpeed = fixed_t::FromRaw( 0x1000 );
	player_t *const player = mo->player;

	if (( mo->velx > -stopSpeed ) && ( mo->velx < stopSpeed ) &&
		( mo->vely > -stopSpeed ) && ( mo->vely < stopSpeed ) &&
		(( player->cmd.ucmd.forwardmove | player->cmd.ucmd.sidemove ) == 0 ))
	{
		if ( CLIENT_PREDICT_IsPredicting() == false )
		{
			// [BC] In client mode we don't know other players' move inputs, so the server tells us
			// when to idle them; only the local player may decide it for themselves.
			if (( NETWORK_InClientMode() == false ) || (( player - players ) == consoleplayer ))
				mo->PlayIdle();
		}

		mo->velx = 0;
		mo->vely = 0;
		mo->flags4 &= ~MF4_SCROLLMOVE;
		player->velx = 0;
		player->vely = 0;
	}
}

// The crouch depth at which a move counts as "crouching", derived from the pawn's own authored
// Player.CrouchScale rather than a fixed constant -- a class that only crouches a little still
// registers as crouched at its own midpoint.
bool IsCrouchedEnough( player_t *player )
{
	const float halfWay = QCrouchHalfWay( FIXED2FLOAT( player->mo->CrouchScale ));
	return FIXED2FLOAT( player->crouchfactor ) < halfWay;
}

// Defined with the jump helpers further down; the traversal moves need it first.
void TraceForWall( APlayerPawn *mo, angle_t angle, FTraceResults &trace );
void SpawnEffectActor( player_t *player, int slot );

// [rc4l] Whether this pawn is the one whose cosmetics WE draw and hear. A dedicated server must
// answer no for everyone: `consoleplayer` is 0 there, which is a real connected client, so a bare
// consoleplayer test silently makes the server emit player 0's dust and looping sounds -- and only
// player 0's. Prediction is excluded too, or a re-predicted tic emits a second puff.
bool IsLocalCosmeticPlayer( player_t *player )
{
	if ( NETWORK_GetState() == NETSTATE_SERVER )
		return false;
	if ( CLIENT_PREDICT_IsPredicting() )
		return false;
	return ( player - players ) == consoleplayer;
}

// Whether a footstep should sound this tic. Air-wall-running counts as footing even though the pawn
// is airborne -- that is the point of running along a wall.
bool ShouldPlayFootsteps( player_t *player, ticcmd_t *cmd )
{
	APlayerPawn *const mo = player->mo;

	// Only the local player's own footing is known here; other players' would need replication.
	if ( IsLocalCosmeticPlayer( player ) == false )
		return false;

	if ((( mo->isAirWallRunning == false ) && ( player->onground == false )) ||
		( mo->waterlevel >= 2 ) || ( mo->flags & MF_NOGRAVITY ))
	{
		return false;
	}

	switch ( MoveTierOf( player, cmd ))
	{
	case QTIER_RUN:			if ( mo->FootstepsEnabled2 == false ) return false; break;
	case QTIER_CROUCH_WALK:	if ( mo->FootstepsEnabled3 == false ) return false; break;
	case QTIER_CROUCH_RUN:	if ( mo->FootstepsEnabled4 == false ) return false; break;
	default:				if ( mo->FootstepsEnabled1 == false ) return false; break;
	}

	// Below a third of the pawn's base speed there is no stride to make a sound for -- this is what
	// stops a player nudging a wall from machine-gunning footsteps.
	const float speed2D = QLength2D( FIXED2FLOAT( mo->velx ), FIXED2FLOAT( mo->vely ));
	return speed2D >= FIXED2FLOAT( mo->Speed ) * 3.0f;
}

// Footsteps are LOCAL-PLAYER ONLY for the same reason as the traversal loops: replicating a step
// per stride per player is exactly the recurring traffic this port refuses to add.
void PlayFootsteps( player_t *player, ticcmd_t *cmd )
{
	APlayerPawn *const mo = player->mo;
	if ( CLIENT_PREDICT_IsPredicting() )
		return;

	if ( ShouldPlayFootsteps( player, cmd ) == false )
	{
		// Re-arm at half the interval so the first step after starting to move lands promptly
		// rather than a full stride late.
		mo->stepInterval = mo->FootstepInterval / 2;
		return;
	}

	if ( mo->stepInterval > 0 )
	{
		mo->stepInterval--;
		return;
	}

	if (( mo->mvFlags & MV_SILENT ) == 0 )
		S_Sound( mo, CHAN_AUTO, "*footstep", mo->FootstepVolume, ATTN_NORM );
	SpawnEffectActor( player, EA_FOOTSTEP );
	mo->stepInterval = mo->FootstepInterval;
}

// Emit a traversal move's cosmetic actor. CLIENTSIDEONLY and local-player-only: nothing is
// replicated, so this costs zero bytes. The consequence, stated plainly in the README, is that you
// see your own dust and not other players' -- a client cannot know their slide state without the
// server telling it, and that telling is exactly the recurring traffic this port refuses to add.
void SpawnEffectActor( player_t *player, int slot )
{
	APlayerPawn *const mo = player->mo;
	if (( slot < 0 ) || ( slot >= EA_COUNT ))
		return;

	const PClass *const type = mo->EffectActors[slot];
	if ( type == NULL )
		return;
	// [rc4l] Safe by construction rather than trusting every caller: this is the function with the
	// side effect, so it is the one that must never fire on a server.
	if ( IsLocalCosmeticPlayer( player ) == false )
		return;

	AActor *const effect = Spawn( type, mo->x, mo->y, mo->z, ALLOW_REPLACE );
	if ( effect != NULL )
		effect->NetworkFlags |= NETFL_CLIENTSIDEONLY;
}

// Start or stop a looping traversal sound on a transition, and emit its dust on the authored
// interval while it runs. `wasActive` is last tic's answer; only the edges touch the sound channel,
// because restarting a looping sound every tic is audible as a stutter.
//
// LOCAL PLAYER ONLY, and deliberately so. Zandronum has SERVERCOMMANDS_SoundActor but no matching
// stop-sound command, so a loop started remotely could never be ended -- remote listeners would be
// left with a slide sound running forever. Broadcasting it would also be recurring traffic, which
// is what this port is explicitly avoiding. You hear your own traversal; other players' is silent
// until a stop-sound command exists to pair with it.
void UpdateLoopingMove( player_t *player, bool isActive, bool wasActive, const char *sound,
	int &effectTics, int interval, int effectSlot )
{
	APlayerPawn *const mo = player->mo;

	// Local player only, never on the server, and never during prediction: a re-predicted tic would
	// restart the loop and stutter it.
	if ( IsLocalCosmeticPlayer( player ) == false )
		return;

	const bool silent = ( mo->mvFlags & MV_SILENT ) != 0;

	if ( isActive && !wasActive && !silent )
		S_Sound( mo, CHAN_BODY | CHAN_LOOP, sound, 1, ATTN_NORM );
	else if ( !isActive && wasActive )
		S_StopSound( mo, CHAN_BODY );

	if ( isActive && ShouldEmitEffect( effectTics, interval ))
		SpawnEffectActor( player, effectSlot );
}

// Sweep for a wall to run along, and spend a tic of the meter when one is found. Unlike wall climb
// this does not change velocity -- it is a state the mod reacts to (via ACS/SBARINFO) plus the
// footstep and effect hooks, matching Q-Zandronum.
void UpdateAirWallRun( player_t *player, const QVec3 &vel, const QVec3 &wish,
	float viewAngleDegrees, ticcmd_t *cmd )
{
	APlayerPawn *const mo = player->mo;
	bool running = false;

	const fixed_t speed2D = FLOAT2FIXED( QLength2D( vel.x, vel.y ));

	if (( IsCrouchedEnough( player ) == false ) &&
		( speed2D >= mo->AirWallRunMinVelocity ) &&
		( QLength3D( wish ) > 0.0f ) &&
		HasCharge( mo->airWallRunTics ))
	{
		QVec3 accelDir;
		accelDir.x = FIXED2FLOAT( fixed_t( cmd->ucmd.forwardmove ));
		accelDir.y = -FIXED2FLOAT( fixed_t( cmd->ucmd.sidemove )) * 1.25f;
		accelDir.z = 0.0f;
		QVectorRotate( accelDir.x, accelDir.y, viewAngleDegrees );
		accelDir = QMakeUnit( accelDir );

		FTraceResults trace;
		for ( int i = 0; i < 16; ++i )
		{
			const angle_t angle = static_cast<angle_t>( i ) * ( ANGLE_MAX / 16 );
			TraceForWall( mo, angle, trace );
			if (( trace.HitType == TRACE_HitWall ) && ( trace.Line != NULL ))
			{
				QVec3 wallDir;
				wallDir.x = FIXED2FLOAT( trace.Line->dx );
				wallDir.y = FIXED2FLOAT( trace.Line->dy );
				wallDir.z = 0.0f;
				wallDir = QMakeUnit( wallDir );

				running = AirWallRunEngages( QDotProduct( accelDir, wallDir ));
				break;
			}
		}
	}

	mo->isAirWallRunning = running;
	if ( running )
		mo->airWallRunTics = SpendCharge( mo->airWallRunTics );
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

bool MovePlayerQuake( player_t *player, ticcmd_t *cmd )
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
		// Same reasoning as the climb path: this returns early, so nothing below would clear these.
		mo->isCrouchSliding = false;
		mo->isWallClimbing = false;
		mo->isAirWallRunning = false;
		// Swimming and flying already used the jump key as vertical steering this tic.
		return true;
	}

	QVectorRotate( wish.x, wish.y, viewAngleDegrees );
	wish = QMakeUnit( wish );

	// The cap is resolved BEFORE this tic's acceleration, so speed already earned is kept.
	const float localCap = QLocalVelocityCap( FIXED2FLOAT( mo->VelocityCap ),
		QLength2D( vel.x, vel.y ));

	const bool airborne = ( player->onground == false ) ||
		((( cmd->ucmd.buttons & BT_JUMP ) != 0 ) && ( player->jumpTics <= 0 ));

	const bool isSlider = ( mo->mvFlags & MV_CROUCHSLIDE ) != 0;
	const bool isClimber = ( mo->mvFlags & MV_WALLCLIMB ) != 0;
	const bool isWallRunner = ( mo->mvFlags & MV_AIRWALLRUN ) != 0;
	bool isSliding = false;
	bool isClimbing = false;

	// Wall climb pre-empts both the ground and air paths: holding jump against a wall with charge
	// left replaces horizontal acceleration with a vertical crawl. Deliberately NOT gated on being
	// grounded -- the whole move is jumping at a wall and climbing it, and `airborne` is true the
	// moment jump is pressed anyway, so a grounded test would make it unreachable.
	if ( isClimber && (( cmd->ucmd.buttons & BT_JUMP ) != 0 ) && HasCharge( mo->wallClimbTics ))
	{
		FTraceResults climbTrace;
		TraceForWall( mo, mo->angle, climbTrace );
		isClimbing = ( climbTrace.HitType == TRACE_HitWall );
	}

	if ( isClimbing )
	{
		vel.z = FIXED2FLOAT( mo->WallClimbSpeed );
		mo->wallClimbTics = SpendCharge( mo->wallClimbTics );
		StoreVelocity( player, vel );
		mo->isWallClimbing = true;
		UpdateLoopingMove( player, true, mo->isWallClimbing, "*wallclimb",
			mo->wallClimbEffectTics, mo->WallClimbEffectInterval, EA_WALL_CLIMB );
		mo->isWallClimbing = true;
		// This path returns before the ground/air branches, so the other two states are cleared
		// here rather than left holding whatever they were when the climb started.
		mo->isCrouchSliding = false;
		mo->isAirWallRunning = false;
		// Holding jump IS the climb input; firing a jump too would kick the player off the wall.
		return true;
	}

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

		if ( isWallRunner )
			UpdateAirWallRun( player, vel, wish, viewAngleDegrees, cmd );

		// Airborne is where charges come back. The slide meter's sign flip lives here: leaving the
		// ground is what releases a lockout (see computation/qtraversal_compute.h).
		if ( isSlider )
			mo->crouchSlideTics = RegenSlideCharge( mo->crouchSlideTics, mo->CrouchSlideMaxTics, mo->CrouchSlideRegen );
		if ( isClimber )
			mo->wallClimbTics = RegenSimpleCharge( mo->wallClimbTics, mo->WallClimbMaxTics, mo->WallClimbRegen );
		if ( isWallRunner )
			mo->airWallRunTics = RegenSimpleCharge( mo->airWallRunTics, mo->AirWallRunMaxTics, mo->AirWallRunRegen );
	}
	else
	{
		// Q-Zandronum also skips this branch for one tic after a ThrustThingZ, via a
		// wasJustThrustedZ flag that only exists alongside their thrust-prediction rework. We did
		// not take that rework, so the guard has nothing to hang off and is omitted; the visible
		// difference is that a Z-thrust does not suppress ground acceleration on its landing tic.
		maxSpeed *= Q_MAX_GROUND_SPEED * moveFactor;

		// UpdateAirWallRun is the only writer of this flag and only runs on the airborne path, so
		// without an explicit clear here it stayed true for the whole time the player was back on
		// the ground -- ACS and SBARINFO would report a wall run that ended tics ago.
		mo->isAirWallRunning = false;

		const bool crouchedEnough = IsCrouchedEnough( player );
		isSliding = CanCrouchSlide( isSlider, crouchedEnough, mo->crouchSlideTics );

		if ( isSliding )
		{
			// A slide accelerates against the FULL ground speed with its own acceleration and does
			// not divide by moveFactor -- that is what makes it carry momentum rather than steer.
			const float granted = QAccelerationSpeed( QDotProduct( wish, vel ), maxSpeed,
				mo->CrouchSlideAcceleration * floorFriction );
			vel.x += wish.x * granted;
			vel.y += wish.y * granted;
			mo->crouchSlideTics = SpendCharge( mo->crouchSlideTics );
		}
		else
		{
			maxSpeed *= moveFactor;
			// Dividing the authored acceleration by moveFactor keeps time-to-top-speed constant
			// across walk and run: a walking player has a lower target but reaches it just as
			// promptly.
			const float accel = ( moveFactor > 0.0f )
				? ( mo->GroundAcceleration / moveFactor * floorFriction )
				: ( mo->GroundAcceleration * floorFriction );

			const float granted = QAccelerationSpeed( QDotProduct( wish, vel ), maxSpeed, accel );
			vel.x += wish.x * granted;
			vel.y += wish.y * granted;

			// Standing upright on the ground actively banks the slide meter into lockout.
			if ( isSlider && !crouchedEnough )
				mo->crouchSlideTics = DrainSlideCharge( mo->crouchSlideTics, mo->CrouchSlideMaxTics, mo->CrouchSlideRegen );
		}

		if ( isClimber )
			mo->wallClimbTics = RegenSimpleCharge( mo->wallClimbTics, mo->WallClimbMaxTics, mo->WallClimbRegen );
		if ( isWallRunner )
			mo->airWallRunTics = RegenSimpleCharge( mo->airWallRunTics, mo->AirWallRunMaxTics, mo->AirWallRunRegen );
	}

	if ( localCap > 0.0f )
	{
		const float scale = QVelocityCapScale( QLength2D( vel.x, vel.y ), localCap );
		vel.x *= scale;
		vel.y *= scale;
	}

	StoreVelocity( player, vel );

	// The looping sound and its dust only start/stop on a transition, so these are driven every
	// tic with the current answer rather than only when something changed.
	if ( isSlider )
	{
		UpdateLoopingMove( player, isSliding, mo->isCrouchSliding, "*crouchslide",
			mo->crouchSlideEffectTics, mo->CrouchSlideEffectInterval, EA_CROUCH_SLIDE );
		mo->isCrouchSliding = isSliding;
	}
	if ( isClimber )
	{
		UpdateLoopingMove( player, false, mo->isWallClimbing, "*wallclimb",
			mo->wallClimbEffectTics, mo->WallClimbEffectInterval, EA_WALL_CLIMB );
		mo->isWallClimbing = false;
	}

	// [BB] Spectators shall stay in their spawn state and don't execute any code pointers.
	// A sliding player is not running, so the run animation is suppressed during a slide.
	if (( CLIENT_PREDICT_IsPredicting() == false ) && player->onground &&
		( player->bSpectating == false ) && ( mo->velx || mo->vely ) && ( isSliding == false ))
	{
		mo->PlayRunning();
	}

	PlayFootsteps( player, cmd );
	return false;
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

	StopAndIdleIfAtRest( pawn );
	return true;
}

namespace {

const int MOVE_BUTTONS = BT_FORWARD | BT_BACK | BT_MOVELEFT | BT_MOVERIGHT;

// Read the pawn's MV_* bits into the engine-free struct the decision functions take.
JumpFlags FlagsOf( const APlayerPawn *mo )
{
	JumpFlags f;
	f.groundSecondJump = ( mo->mvFlags & MV_GROUNDSECONDJUMP ) != 0;
	f.doubleTapJump = ( mo->mvFlags & MV_DOUBLETAPJUMP ) != 0;
	f.user4Jump = ( mo->mvFlags & MV_USER4JUMP ) != 0;
	f.wallJump = ( mo->mvFlags & MV_WALLJUMP ) != 0;
	f.wallJumpV2 = ( mo->mvFlags & MV_WALLJUMPV2 ) != 0;
	f.absoluteSecondJump = ( mo->mvFlags & MV_ABSOLUTESECONDJUMP ) != 0;
	f.edgeJump = ( mo->mvFlags & MV_EDGEJUMP ) != 0;
	return f;
}

// Trace horizontally for a wall at roughly chest height. The 24-unit floor on the distance is
// Q-Zandronum's: shorter traces produce false hits at diagonal angles.
void TraceForWall( APlayerPawn *mo, angle_t angle, FTraceResults &trace )
{
	const angle_t fineAngle = angle >> ANGLETOFINESHIFT;
	const fixed_t stepOrView = ( mo->MaxStepHeight < mo->ViewHeight ) ? mo->MaxStepHeight : mo->ViewHeight;
	const fixed_t offset = stepOrView - 8 * FRACUNIT;
	const fixed_t traceZ = mo->z + (( offset > FRACUNIT ) ? offset : fixed_t( FRACUNIT ));
	const fixed_t reach = mo->radius + 8 * FRACUNIT;
	const fixed_t distance = ( reach > 24 * FRACUNIT ) ? reach : fixed_t( 24 * FRACUNIT );

	Trace( mo->x, mo->y, traceZ, mo->Sector,
		finecosine[fineAngle], finesine[fineAngle], 0, distance,
		MF_SOLID, ML_BLOCKING | ML_3DMIDTEX_IMPASS, mo, trace, TRACE_NoSky );
}

// Sweep 16 evenly spaced directions looking for a wall to kick off. Returns true on the first hit,
// leaving `trace` describing it (WALLJUMPV2 needs the line to derive its normal).
bool FindNearbyWall( APlayerPawn *mo, FTraceResults &trace )
{
	for ( int i = 0; i < 16; ++i )
	{
		const angle_t angle = static_cast<angle_t>( i ) * ( ANGLE_MAX / 16 );
		TraceForWall( mo, angle, trace );
		if ( trace.HitType == TRACE_HitWall )
			return true;
	}
	return false;
}

// The pawn's facing-relative wish direction, scaled to `magnitude`. Shared by the main jump's
// horizontal component and the second jump's.
void JumpDirection( APlayerPawn *mo, ticcmd_t *cmd, fixed_t magnitude, fixed_t &outX, fixed_t &outY )
{
	outX = 0;
	outY = 0;
	if (( magnitude == 0 ) || ( cmd == NULL ))
		return;

	float x = FIXED2FLOAT( FixedMul( fixed_t( cmd->ucmd.forwardmove ), mo->ForwardMove2 ));
	float y = FIXED2FLOAT( FixedMul( fixed_t( -cmd->ucmd.sidemove ), mo->SideMove2 ));
	const QVec3 unit = QMakeUnit( QVec3{ x, y, 0.0f } );

	const angle_t fineAngle = mo->angle >> ANGLETOFINESHIFT;
	const float cosine = FIXED2FLOAT( finecosine[fineAngle] );
	const float sine = FIXED2FLOAT( finesine[fineAngle] );

	const float worldX = unit.x * cosine - unit.y * sine;
	const float worldY = unit.x * sine + unit.y * cosine;

	outX = FixedMul( FLOAT2FIXED( worldX ), magnitude );
	outY = FixedMul( FLOAT2FIXED( worldY ), magnitude );
}

void PlayJumpSound( APlayerPawn *mo, const char *sound )
{
	if ( mo->mvFlags & MV_SILENT )
		return;
	// [BB] We may not play the sound while predicting, otherwise it'll stutter.
	if ( CLIENT_PREDICT_IsPredicting() == false )
		S_Sound( mo, CHAN_BODY, sound, 1, ATTN_NORM );
	// [EP] Inform the other clients to play the sound.
	if ( NETWORK_GetState() == NETSTATE_SERVER )
	{
		SERVERCOMMANDS_SoundActor( mo, CHAN_BODY, sound, 1, ATTN_NORM,
			mo->player - players, SVCF_SKIPTHISCLIENT );
	}
}

} // namespace

bool CheckJumpQuake( player_t *player, ticcmd_t *cmd )
{
	APlayerPawn *const mo = player->mo;

	if (( player->bSpectating == false ) && ( level.IsJumpingAllowed() == false ))
		return true;

	const JumpFlags flags = FlagsOf( mo );

	if ( player->onground )
	{
		const GroundedJumpState grounded = ComputeGroundedState( mo->SecondJumpAmount, flags,
			mo->secondJumpTics, player->jumpTics, int( FIXED2FLOAT( mo->velz )));

		mo->secondJumpsRemaining = grounded.secondJumpsRemaining;
		mo->secondJumpState = grounded.state;
		if ( grounded.resetJumpTics )
			player->jumpTics = mo->JumpDelay;
	}
	else if ( ComputeAirborneArming( mo->secondJumpsRemaining, mo->secondJumpTics, flags,
		( cmd->ucmd.buttons & BT_JUMP ) != 0 ))
	{
		mo->secondJumpState = SJ_AVAILABLE;
	}

	FTraceResults wallTrace;
	wallTrace.HitType = TRACE_HitNone;

	if ( mo->secondJumpState == SJ_AVAILABLE )
	{
		bool doubleTapFired = false;
		if ( flags.doubleTapJump )
		{
			// Spelled out rather than abs(): fixed_strong.h declares abs(Fixed), and the short
			// operands are convertible to it, so a bare abs() silently returns a Fixed here.
			const int forwardInput = cmd->ucmd.forwardmove;
			const int sideInput = cmd->ucmd.sidemove;
			const int tapValue = ( forwardInput < 0 ? -forwardInput : forwardInput ) +
				( sideInput < 0 ? -sideInput : sideInput );
			const DoubleTapResult tap = ComputeDoubleTap( tapValue, mo->lastTapValue,
				mo->secondJumpTics, cmd->ucmd.buttons & MOVE_BUTTONS,
				player->oldbuttons & MOVE_BUTTONS, mo->lastMoveButtonsBefore, mo->DoubleTapMaxTics );

			doubleTapFired = tap.fired;
			mo->lastTapValue = tap.lastTapValue;
			mo->secondJumpTics = tap.secondJumpTics;
			mo->lastMoveButtonsBefore = tap.lastMoveButtonsBefore;
		}

		const bool user4JustPressed = flags.user4Jump &&
			(( cmd->ucmd.buttons & BT_USER4 ) != 0 ) && (( player->oldbuttons & BT_USER4 ) == 0 );
		const bool jumpJustPressed =
			(( cmd->ucmd.buttons & BT_JUMP ) != 0 ) && (( player->oldbuttons & BT_JUMP ) == 0 );

		if ( ComputeSecondJumpTriggered( flags, doubleTapFired, user4JustPressed, jumpJustPressed ))
			mo->secondJumpState = SJ_READY;

		// A wall jump is only granted next to a wall; without one the jump stays armed rather than
		// being consumed, so the player can still spend it after they reach a surface.
		if (( mo->secondJumpState == SJ_READY ) && ( flags.wallJump || flags.wallJumpV2 ) &&
			( player->onground == false ))
		{
			if ( FindNearbyWall( mo, wallTrace ) == false )
				mo->secondJumpState = SJ_AVAILABLE;
		}
	}

	// Crouching while still rising off a ledge trims the climb to a plain jump height, which is how
	// Q-Zandronum keeps ledge-climbing from stacking with a jump.
	const bool isClimbingLedge = player->onground && ( mo->velz > 0 ) &&
		(( cmd->ucmd.buttons & BT_CROUCH ) != 0 );

	if ( isClimbingLedge )
	{
		if ( mo->velz > mo->JumpZ )
			mo->velz = mo->JumpZ;
	}
	else if ( player->onground && ( mo->secondJumpState != SJ_READY ) &&
		(( cmd->ucmd.buttons & BT_JUMP ) != 0 ) &&
		(( player->jumpTics == 0 ) || ( mo->flags2 & MF2_ONMOBJ )))
	{
		const bool isEdgeJump = flags.edgeJump && (( cmd->ucmd.buttons & BT_CROUCH ) == 0 );

		fixed_t jumpVelX, jumpVelY;
		JumpDirection( mo, cmd, mo->JumpXY, jumpVelX, jumpVelY );

		fixed_t jumpVelZ = mo->JumpZ;
		if ( player->cheats & CF_HIGHJUMP )
			jumpVelZ *= 2;
		if ( mo->floorsector->GetFlags( sector_t::floor ) & PLANEF_SPRINGPAD )
			jumpVelZ /= 2;

		if ( isEdgeJump && ( mo->velz > 0 ))
			PlayJumpSound( mo, "*edgejump" );
		else if ( mo->JumpSoundDelay <= 0 )
			PlayJumpSound( mo, "*jump" );
		mo->JumpSoundDelay = 3;

		mo->flags2 &= ~MF2_ONMOBJ;

		mo->velx += jumpVelX;
		mo->vely += jumpVelY;
		mo->velz = fixed_t::FromRaw( ComputeMainJumpVelZ( mo->velz.Raw(), jumpVelZ.Raw(),
			isEdgeJump ));

		ApplyElevatorJump( mo );

		player->jumpTics = ComputeJumpTics(
			( zacompatflags & ZACOMPATF_SKULLTAG_JUMPING ) != 0,
			( player->cheats & CF_HIGHJUMP ) != 0,
			( mo->floorsector->GetFlags( sector_t::floor ) & PLANEF_SPRINGPAD ) != 0,
			TICRATE );

		// [Leo] Inform the client of the jumpTics change.
		if ( NETWORK_GetState() == NETSTATE_SERVER )
			SERVERCOMMANDS_SetLocalPlayerJumpTics( player - players );
	}
	else if ( mo->secondJumpState == SJ_READY )
	{
		fixed_t pushX = 0, pushY = 0;

		if ( flags.wallJumpV2 && ( wallTrace.HitType == TRACE_HitWall ) && ( wallTrace.Line != NULL ))
		{
			// Push along the wall's normal rather than the player's facing -- that is what makes a
			// V2 wall jump feel like kicking off the surface instead of steering in mid-air.
			const angle_t lineAngle =
				R_PointToAngle2( 0, 0, wallTrace.Line->dx, wallTrace.Line->dy ) - ANG90;
			pushX = FixedMul( finecosine[lineAngle >> ANGLETOFINESHIFT], mo->SecondJumpXY );
			pushY = FixedMul( finesine[lineAngle >> ANGLETOFINESHIFT], mo->SecondJumpXY );
		}
		else
		{
			JumpDirection( mo, cmd, mo->SecondJumpXY, pushX, pushY );
		}

		if ( flags.absoluteSecondJump || flags.wallJumpV2 )
		{
			mo->velx = pushX;
			mo->vely = pushY;
		}
		else
		{
			mo->velx += pushX;
			mo->vely += pushY;
		}

		mo->velz = fixed_t::FromRaw( ComputeSecondJumpVelZ( mo->velz.Raw(),
			mo->SecondJumpZ.Raw(), ( player->cheats & CF_HIGHJUMP ) != 0 ));

		ApplyElevatorJump( mo );

		PlayJumpSound( mo, "*secondjump" );

		player->jumpTics = mo->JumpDelay;
		mo->secondJumpTics = mo->SecondJumpDelay;
		// A negative remaining count is "unlimited" and must not be decremented into range.
		if ( mo->secondJumpsRemaining > 0 )
			mo->secondJumpsRemaining--;
		mo->secondJumpState = SJ_NOT_AVAILABLE;

		if ( NETWORK_GetState() == NETSTATE_SERVER )
			SERVERCOMMANDS_SetLocalPlayerJumpTics( player - players );
	}

	if ( mo->JumpSoundDelay > 0 )
		mo->JumpSoundDelay--;

	// The cooldown counts down toward 0 from above; a negative value is the double-tap window and
	// counts UP toward 0, so one decrement here would run it the wrong way.
	if ( mo->secondJumpTics > 0 )
		mo->secondJumpTics--;
	else if ( mo->secondJumpTics < 0 )
		mo->secondJumpTics++;

	return true;
}

} // namespace quakemove
} // namespace zx
