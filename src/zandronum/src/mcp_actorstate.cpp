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

CCMD( dumpactor )
{
	AActor *mo = players[consoleplayer].mo;
	if ( argv.argc() >= 2 )
	{
		FActorIterator it( atoi( argv[1] ) );
		mo = it.Next();
	}

	Printf( "MCP_ACTOR\n" );
	if ( mo == NULL ) { Printf( "actor none\n" ); return; }

	Printf( "class %s\n", mo->GetClass()->TypeName.GetChars() );
	Printf( "health %d\n", mo->health );
	Printf( "pos %.1f %.1f %.1f\n", FIXED2FLOAT( mo->x ), FIXED2FLOAT( mo->y ), FIXED2FLOAT( mo->z ) );
	Printf( "angle %.1f\n", mo->angle / float( ANGLE_1 ) );
	MCP_PrintState( "state", mo );

	if ( mo->player != NULL )
	{
		if ( mo->player->ReadyWeapon != NULL )
			Printf( "weapon %s\n", mo->player->ReadyWeapon->GetClass()->TypeName.GetChars() );
		Printf( "morphtics %d\n", mo->player->morphTics );
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
