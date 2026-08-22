// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

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

// mcp_look <pitchDeg> [yawDeg] -- set the player's view pitch (and optionally yaw)
// directly, for driving/screenshotting from the MCP. Pitch sign follows ZDoom:
// positive = look DOWN, negative = look UP. Clamped by the engine to maxviewpitch
// on the next tic. Yaw is absolute compass degrees (0 = east, 90 = north).
CCMD( mcp_look )
{
	AActor *mo = players[consoleplayer].mo;
	if ( mo == NULL ) { Printf( "MCP_LOOK none\n" ); return; }
	if ( argv.argc() >= 2 )
	{
		// Whole degrees. int * ANGLE_1 -> fixed_t (same idiom as p_user.cpp pitch
		// clamp). Positive = look DOWN, negative = look UP (ZDoom convention).
		int degi = atoi( argv[1] );
		mo->pitch = degi * ANGLE_1;
	}
	if ( argv.argc() >= 3 )
	{
		int yawi = atoi( argv[2] );
		mo->angle = (angle_t)( yawi * ANGLE_1 );
	}
	// mcp_look <pitch> <yaw> <x> <y> [z] also teleports (no cheat check), for driving
	// the camera to a spot for screenshots. With a 5th arg it becomes a fly-cam at that
	// height (no gravity) -- e.g. high above the map looking straight down.
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

// Is (x,y) actually inside this convex subsector? (Its BSP leaf is returned even for
// a hole, where the point falls outside the degenerate leaf's polygon.)
static bool ZX_PointInSub( fixed_t x, fixed_t y, subsector_t *sub )
{
	if ( sub == NULL || sub->numlines < 3 ) return false;
	double pxf = FIXED2FLOAT( x ), pyf = FIXED2FLOAT( y );
	for ( DWORD i = 0; i < sub->numlines; i++ )
	{
		seg_t *seg = &sub->firstline[i];
		double vx1 = seg->v1->fx, vy1 = seg->v1->fy, vx2 = seg->v2->fx, vy2 = seg->v2->fy;
		double cross = ( vx2 - vx1 ) * ( pyf - vy1 ) - ( vy2 - vy1 ) * ( pxf - vx1 );
		if ( cross > 0.0 ) return false; // outside (interior is on the right of v1->v2)
	}
	return true;
}

static bool ZX_PointInTri( double px, double py, double ax, double ay, double bx, double by, double cx, double cy )
{
	double d1 = ( px - bx ) * ( ay - by ) - ( ax - bx ) * ( py - by );
	double d2 = ( px - cx ) * ( by - cy ) - ( bx - cx ) * ( py - cy );
	double d3 = ( px - ax ) * ( cy - ay ) - ( cx - ax ) * ( py - ay );
	bool neg = ( d1 < 0 ) || ( d2 < 0 ) || ( d3 < 0 );
	bool pos = ( d1 > 0 ) || ( d2 > 0 ) || ( d3 > 0 );
	return !( neg && pos );
}

// mcp_findholes [step] -- scan a grid; report points that are BOTH uncovered by any real
// subsector AND not covered by the flat-holes fill (i.e. the holes still visible on screen).
CCMD( mcp_findholes )
{
	extern subsector_t *R_PointInSubsector( fixed_t x, fixed_t y );
	extern const float *FlatHoles_Get( int sectornum, int *numverts );
	int step = ( argv.argc() >= 2 ) ? atoi( argv[1] ) : 48;
	if ( step < 8 ) step = 8;

	fixed_t minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
	for ( int i = 0; i < numvertexes; i++ )
	{
		if ( vertexes[i].x < minx ) minx = vertexes[i].x;
		if ( vertexes[i].y < miny ) miny = vertexes[i].y;
		if ( vertexes[i].x > maxx ) maxx = vertexes[i].x;
		if ( vertexes[i].y > maxy ) maxy = vertexes[i].y;
	}

	Printf( "MCP_HOLES begin step=%d\n", step );
	int *tally = new int[numsectors]; memset( tally, 0, sizeof(int) * numsectors );
	double *sumx = new double[numsectors]; memset( sumx, 0, sizeof(double) * numsectors );
	double *sumy = new double[numsectors]; memset( sumy, 0, sizeof(double) * numsectors );
	int total = 0;
	for ( fixed_t y = miny; y <= maxy; y += step * FRACUNIT )
	{
		for ( fixed_t x = minx; x <= maxx; x += step * FRACUNIT )
		{
			subsector_t *ss = R_PointInSubsector( x, y );
			if ( ZX_PointInSub( x, y, ss ) ) continue; // covered by a real subsector
			// A real (visible) hole borders floor; deep out-of-map void does not. Require at
			// least one 4-neighbour to be covered by a real subsector.
			fixed_t st = step * FRACUNIT;
			bool nearFloor =
				ZX_PointInSub( x + st, y, R_PointInSubsector( x + st, y ) ) ||
				ZX_PointInSub( x - st, y, R_PointInSubsector( x - st, y ) ) ||
				ZX_PointInSub( x, y + st, R_PointInSubsector( x, y + st ) ) ||
				ZX_PointInSub( x, y - st, R_PointInSubsector( x, y - st ) );
			if ( !nearFloor ) continue; // deep void, not a visible hole
			int sn = ( ss && ss->render_sector ) ? ss->render_sector->sectornum : -1;
			// covered by the flat-holes fill?
			double pxf = FIXED2FLOAT( x ), pyf = FIXED2FLOAT( y );
			bool filled = false;
			if ( sn >= 0 )
			{
				int nv = 0;
				const float *tf = FlatHoles_Get( sn, &nv );
				for ( int k = 0; tf != NULL && k + 5 < nv * 2; k += 6 )
				{
					if ( ZX_PointInTri( pxf, pyf, tf[k], tf[k+1], tf[k+2], tf[k+3], tf[k+4], tf[k+5] ) )
					{ filled = true; break; }
				}
			}
			if ( filled ) continue; // our fill draws here -> not a visible hole
			total++;
			if ( sn >= 0 && sn < numsectors ) { tally[sn]++; sumx[sn] += pxf; sumy[sn] += pyf; }
		}
	}
	// report the sectors with the most uncovered grid points (the biggest holes)
	for ( int r = 0; r < 12; r++ )
	{
		int best = -1;
		for ( int s = 0; s < numsectors; s++ ) if ( tally[s] > 0 && ( best < 0 || tally[s] > tally[best] ) ) best = s;
		if ( best < 0 ) break;
		// centroid of the UNCOVERED points themselves -> lands on the actual hole
		Printf( "MCP_HOLESEC sector=%d uncovered=%d at %.0f %.0f\n", best, tally[best],
			sumx[best] / tally[best], sumy[best] / tally[best] );
		tally[best] = 0;
	}
	Printf( "MCP_HOLES total=%d\n", total );
	delete[] tally;
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
