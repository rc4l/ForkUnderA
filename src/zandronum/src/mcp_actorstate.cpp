//
// mcp_actorstate.cpp -- read-only DECORATE/actor state inspection (overlay file).
// Dumps an actor's live state: class, health, position, current DECORATE state
// (sprite/frame/tics, e.g. "TNT1 A 0"), inventory chain, and -- for players --
// ready weapon and morph status. Plus the actors near the player. All reads.
//
#include "doomtype.h"
#include "c_dispatch.h"
#include "actor.h"
#include "d_player.h"
#include "doomstat.h"
#include "g_shared/a_pickups.h"
#include "r_state.h"
#include "r_data/sprites.h"
#include "m_fixed.h"
#include "tables.h"
#include "p_local.h"
#include <stdlib.h>

static void MCP_PrintState( const char *label, AActor *mo )
{
	if ( mo->state != NULL && mo->state->sprite < sprites.Size() )
	{
		Printf( "%s %s %c %d\n", label, sprites[mo->state->sprite].name,
			(char)( 'A' + mo->state->Frame ), mo->state->Tics );
	}
}

// [rc4l] Scale to an int before printing: ZDoom's Printf renders some ordinary float values as
// "-NaN", so %f output is not evidence -- see features/quake-movement/README.md.
static int MCP_Milli( float v ) { return (int)( v * 1000.0f ); }
static int MCP_MilliFixed( fixed_t v ) { return (int)( FIXED2FLOAT( v ) * 1000.0f ); }

static const char *MCP_ClassName( const PClass *cls ) { return cls ? cls->TypeName.GetChars() : "none"; }

CCMD( dumpactor )
{
	AActor *mo = players[consoleplayer].mo;
	if ( argv.argc() >= 2 )
	{
		// [rc4l] "p<N>" selects players[N].mo. A -host server has no console player at all, so
		// without this there is no way to read the authoritative side of anything.
		if ( argv[1][0] == 'p' || argv[1][0] == 'P' )
		{
			const int playerIndex = atoi( argv[1] + 1 );
			mo = ( playerIndex >= 0 && playerIndex < MAXPLAYERS && playeringame[playerIndex] )
				? players[playerIndex].mo : NULL;
		}
		else
		{
			FActorIterator it( atoi( argv[1] ) );
			mo = it.Next();
		}
	}

	Printf( "MCP_ACTOR\n" );
	if ( mo == NULL ) { Printf( "actor none\n" ); return; }

	Printf( "class %s\n", mo->GetClass()->TypeName.GetChars() );
	Printf( "health %d\n", mo->health );
	Printf( "pos %.1f %.1f %.1f\n", FIXED2FLOAT( mo->x ), FIXED2FLOAT( mo->y ), FIXED2FLOAT( mo->z ) );
	Printf( "angle %.1f\n", mo->angle / float( ANGLE_1 ) );
	MCP_PrintState( "state", mo );
	// [rc4l] Movement-model state (features/quake-movement). mvFlags lives on AActor, so it is
	// dumped for every actor; MvType is pawn-only. Without these the movement model has no readback
	// path at all -- A_CheckFlag can see individual mvFlags bits, but nothing could observe MvType.
	Printf( "mvflags %08x\n", (unsigned int)mo->mvFlags );

	if ( mo->player != NULL )
	{
		if ( mo->player->ReadyWeapon != NULL )
			Printf( "weapon %s\n", mo->player->ReadyWeapon->GetClass()->TypeName.GetChars() );
		Printf( "morphtics %d\n", mo->player->morphTics );
		// [rc4l] Type-checked, not a bare static_cast: dumpactor takes an arbitrary TID, and a
		// debug command must not be the thing that crashes on an actor that carries a player
		// pointer without being a pawn.
		if ( mo->IsKindOf( RUNTIME_CLASS( APlayerPawn )))
		{
			APlayerPawn *const pawn = static_cast<APlayerPawn *>( mo );
			Printf( "mvtype %d\n", pawn->MvType );
			// [rc4l] Second-jump state (features/quake-movement stage 3). Without this the state
			// machine is unobservable and a misfiring double jump can only be guessed at.
			Printf( "secondjump state %d remaining %d tics %d amount %d\n",
				pawn->secondJumpState, pawn->secondJumpsRemaining, pawn->secondJumpTics,
				pawn->SecondJumpAmount );
			Printf( "jumptics %d velz %.2f onground %d\n",
				mo->player->jumpTics, FIXED2FLOAT( mo->velz ), mo->player->onground ? 1 : 0 );
			Printf( "buttons %08x oldbuttons %08x\n",
				(unsigned int)mo->player->cmd.ucmd.buttons, (unsigned int)mo->player->oldbuttons );
			// [rc4l] Traversal charges (features/quake-movement stage 4). Scaled to integers because
			// ZDoom's Printf renders some ordinary float values as "-NaN" -- see the feature README.
			Printf( "traversal slide_x100 %d climb_x100 %d wallrun_x100 %d\n",
				(int)( pawn->crouchSlideTics * 100.0f ), (int)( pawn->wallClimbTics * 100.0f ),
				(int)( pawn->airWallRunTics * 100.0f ));
			Printf( "traversal sliding %d climbing %d wallrunning %d crouchfactor_x100 %d\n",
				pawn->isCrouchSliding ? 1 : 0, pawn->isWallClimbing ? 1 : 0,
				pawn->isAirWallRunning ? 1 : 0,
				(int)( FIXED2FLOAT( mo->player->crouchfactor ) * 100.0f ));
			Printf( "qtune gaccel %.3f gfric %.3f cpmaccel %.3f cpmangle %.4f airaccel %.3f cap %.3f\n",
				pawn->GroundAcceleration, pawn->GroundFriction, pawn->CpmAirAcceleration,
				pawn->CpmMaxForwardAngleRad, FIXED2FLOAT( pawn->AirAcceleration ),
				FIXED2FLOAT( pawn->VelocityCap ));
			Printf( "vel %.3f %.3f speed2d %.3f fwd %d side %d\n",
				FIXED2FLOAT( mo->velx ), FIXED2FLOAT( mo->vely ),
				sqrtf( FIXED2FLOAT( mo->velx ) * FIXED2FLOAT( mo->velx ) +
					FIXED2FLOAT( mo->vely ) * FIXED2FLOAT( mo->vely )),
				(int)mo->player->cmd.ucmd.forwardmove, (int)mo->player->cmd.ucmd.sidemove );
			// [rc4l] The full authored surface, in thousandths. This is what lets the master E2E
			// assert that every new Player.* property actually parsed and reached the pawn, rather
			// than inferring it from behaviour that a default would also produce.
			Printf( "qjump jumpz %d jumpxy %d jumpdelay %d sjz %d sjxy %d sjdelay %d sjamount %d taptics %d\n",
				MCP_MilliFixed( pawn->JumpZ ), MCP_MilliFixed( pawn->JumpXY ), pawn->JumpDelay,
				MCP_MilliFixed( pawn->SecondJumpZ ), MCP_MilliFixed( pawn->SecondJumpXY ),
				pawn->SecondJumpDelay, pawn->SecondJumpAmount, pawn->DoubleTapMaxTics );
			Printf( "qslide acc %d fric %d max %d regen %d eff %d\n",
				MCP_Milli( pawn->CrouchSlideAcceleration ), MCP_Milli( pawn->CrouchSlideFriction ),
				MCP_Milli( pawn->CrouchSlideMaxTics ), MCP_Milli( pawn->CrouchSlideRegen ),
				pawn->CrouchSlideEffectInterval );
			Printf( "qclimb speed %d fric %d max %d regen %d eff %d\n",
				MCP_MilliFixed( pawn->WallClimbSpeed ), MCP_Milli( pawn->WallClimbFriction ),
				MCP_Milli( pawn->WallClimbMaxTics ), MCP_Milli( pawn->WallClimbRegen ),
				pawn->WallClimbEffectInterval );
			Printf( "qwallrun max %d regen %d minvel %d\n",
				MCP_Milli( pawn->AirWallRunMaxTics ), MCP_Milli( pawn->AirWallRunRegen ),
				MCP_MilliFixed( pawn->AirWallRunMinVelocity ));
			Printf( "qtier fwd %d %d %d %d side %d %d %d %d\n",
				MCP_MilliFixed( pawn->ForwardMove1 ), MCP_MilliFixed( pawn->ForwardMove2 ),
				MCP_MilliFixed( pawn->ForwardMove3 ), MCP_MilliFixed( pawn->ForwardMove4 ),
				MCP_MilliFixed( pawn->SideMove1 ), MCP_MilliFixed( pawn->SideMove2 ),
				MCP_MilliFixed( pawn->SideMove3 ), MCP_MilliFixed( pawn->SideMove4 ));
			Printf( "qsteps on %d %d %d %d interval %d vol %d\n",
				pawn->FootstepsEnabled1 ? 1 : 0, pawn->FootstepsEnabled2 ? 1 : 0,
				pawn->FootstepsEnabled3 ? 1 : 0, pawn->FootstepsEnabled4 ? 1 : 0,
				pawn->FootstepInterval, MCP_Milli( pawn->FootstepVolume ));
			Printf( "qcrouch scale %d changespeed %d\n",
				MCP_MilliFixed( pawn->CrouchScale ), MCP_MilliFixed( pawn->CrouchChangeSpeed ));
			Printf( "qeffect slide %s climb %s step %s slidetics %d climbtics %d steptics %d\n",
				MCP_ClassName( pawn->EffectActors[EA_CROUCH_SLIDE] ),
				MCP_ClassName( pawn->EffectActors[EA_WALL_CLIMB] ),
				MCP_ClassName( pawn->EffectActors[EA_FOOTSTEP] ),
				pawn->crouchSlideEffectTics, pawn->wallClimbEffectTics, pawn->stepInterval );
			// [rc4l] The pre-friction velocity the server puts on the wire for Quake pawns. Only
			// the server writes it, so a client dump showing zeroes here is correct, not a failure.
			Printf( "qsrvvel %d %d %d\n",
				MCP_MilliFixed( mo->player->ServerXYZVel[0] ), MCP_MilliFixed( mo->player->ServerXYZVel[1] ),
				MCP_MilliFixed( mo->player->ServerXYZVel[2] ));
		}
	}

	for ( AInventory *item = mo->Inventory; item != NULL; item = item->Inventory )
		Printf( "item %s %d %d\n", item->GetClass()->TypeName.GetChars(), item->Amount, item->MaxAmount );
}

// mcp_look <pitchDeg> [yawDeg] [x y [z]] -- drive the player's view/position for
// screenshotting from the MCP. Pitch follows ZDoom (positive = look DOWN, negative =
// look UP), clamped to maxviewpitch on the next tic. Yaw is absolute compass degrees.
// With x y it teleports (no cheat check); a 5th arg z makes it a no-gravity fly-cam at
// that height (e.g. high above the map looking straight down).
CCMD( mcp_look )
{
	AActor *mo = players[consoleplayer].mo;
	if ( mo == NULL ) { Printf( "MCP_LOOK none\n" ); return; }
	if ( argv.argc() >= 2 )
	{
		int degi = atoi( argv[1] );
		mo->pitch = degi * ANGLE_1;
	}
	if ( argv.argc() >= 3 )
	{
		int yawi = atoi( argv[2] );
		mo->angle = (angle_t)( yawi * ANGLE_1 );
	}
	if ( argv.argc() >= 5 )
	{
		// [rc4l] Teleporting / fly-cam moves the player -- a cheat in online play. Guard it exactly
		// like the warp CCMD: allowed in single-player, for spectators, or with sv_cheats; refused
		// for a normal player in a netgame. The pitch/yaw above stays ungated (it is only view aim).
		if ( players[consoleplayer].bSpectating == false && CheckCheatmode() )
			return;

		fixed_t tx = atoi( argv[3] ) * FRACUNIT;
		fixed_t ty = atoi( argv[4] ) * FRACUNIT;
		if ( argv.argc() >= 6 )
		{
			mo->flags |= MF_NOGRAVITY;
			P_TeleportMove( mo, tx, ty, atoi( argv[5] ) * FRACUNIT, true );
		}
		else
		{
			P_TeleportMove( mo, tx, ty, ONFLOORZ, true );
		}
	}
	Printf( "MCP_LOOK pitchraw %u angleraw %u pos %.0f %.0f\n",
		(unsigned)(angle_t)mo->pitch, (unsigned)mo->angle, FIXED2FLOAT( mo->x ), FIXED2FLOAT( mo->y ) );
}

CCMD( actorsnear )
{
	AActor *me = players[consoleplayer].mo;
	Printf( "MCP_ACTORS\n" );
	if ( me == NULL ) return;
	fixed_t r = ( ( argv.argc() >= 2 ) ? atoi( argv[1] ) : 512 ) * FRACUNIT;

	TThinkerIterator<AActor> it;
	AActor *a;
	while ( ( a = it.Next() ) != NULL )
	{
		if ( a == me ) continue;
		if ( abs( a->x - me->x ) > r || abs( a->y - me->y ) > r ) continue;
		const char *spr = ( a->state != NULL && a->state->sprite < sprites.Size() )
			? sprites[a->state->sprite].name : "----";
		Printf( "near %s %d %.0f %.0f %s\n", a->GetClass()->TypeName.GetChars(), a->health,
			FIXED2FLOAT( a->x ), FIXED2FLOAT( a->y ), spr );
	}
}
