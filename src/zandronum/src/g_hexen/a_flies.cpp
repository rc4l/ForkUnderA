/*
#include "actor.h"
#include "info.h"
#include "m_random.h"
#include "p_local.h"
#include "s_sound.h"
#include "r_defs.h"
#include "thingdef/thingdef.h"
*/

// [rc4l] uzdoom@d37f9cbca -- the Hexen retail beta's buzzing fly (doomed #112).
// Client/server adaptation follows a_bats.cpp's A_BatMove, the closest analogue here:
// the server owns the movement and the sync RNG draws, and pushes the result out.
static FRandom pr_fly("GetOffMeFly");

//===========================================================================
//
// FindCorpse
//
// Finds a corpse to buzz around. We can't use a blockmap check because
// corpses generally aren't linked into the blockmap.
//
//===========================================================================

static AActor *FindCorpse(AActor *fly, sector_t *sec, int recurselimit)
{
	AActor *fallback = NULL;
	sec->validcount = validcount;

	// Search the current sector
	for (AActor *check = sec->thinglist; check != NULL; check = check->snext)
	{
		if (check == fly)
			continue;
		if (!(check->flags & MF_CORPSE))
			continue;
		if (!P_CheckSight(fly, check))
			continue;
		fallback = check;
		if (pr_fly(2))	// 50% chance to try to pick a different corpse
			continue;
		return check;
	}
	if (--recurselimit <= 0 || (fallback != NULL && pr_fly(2)))
	{
		return fallback;
	}
	// Try neighboring sectors
	for (int i = 0; i < sec->linecount; ++i)
	{
		line_t *line = sec->lines[i];
		sector_t *sec2 = (line->frontsector == sec) ? line->backsector : line->frontsector;
		if (sec2 != NULL && sec2->validcount != validcount)
		{
			AActor *neighbor = FindCorpse(fly, sec2, recurselimit);
			if (neighbor != NULL)
			{
				return neighbor;
			}
		}
	}
	return fallback;
}

DEFINE_ACTION_FUNCTION(AActor, A_FlySearch)
{
	// [rc4l] Server-authoritative: picking a corpse draws the sync RNG and changes state, so a
	// client running it independently would pick a different target and desync.
	if ( NETWORK_InClientMode() )
		return;

	// The version from the retail beta is not so great for general use:
	// 1. Pick one of the first fifty thinkers at random.
	// 2. Starting from that thinker, find one that is an actor, not itself,
	//    and within sight. Give up after 100 sequential thinkers.
	// It's effectively useless if there are more than 150 thinkers on a map.
	//
	// So search the sectors instead. We can't potentially find something all
	// the way on the other side of the map and we can't find invisible corpses,
	// but at least we aren't crippled on maps with lots of stuff going on.
	validcount++;
	AActor *other = FindCorpse(self, self->Sector, 5);
	if (other != NULL)
	{
		self->target = other;
		FState *buzz = self->FindState("Buzz");
		self->SetState(buzz);

		// [rc4l] "Buzz" is a named state with no NetworkActorState of its own, so it goes out as
		// an explicit FState via SetThingFrame rather than through SetThingState.
		if ( NETWORK_GetState() == NETSTATE_SERVER )
			SERVERCOMMANDS_SetThingFrame( self, buzz );
	}
}

DEFINE_ACTION_FUNCTION(AActor, A_FlyBuzz)
{
	// [rc4l] Server-authoritative, as A_BatMove is: this moves the actor, sets its velocity and
	// angle and draws the sync RNG several times.
	if ( NETWORK_InClientMode() )
		return;

	AActor *targ = self->target;

	if (targ == NULL || !(targ->flags & MF_CORPSE) || pr_fly() < 5)
	{
		self->SetIdle();
		if ( NETWORK_GetState() == NETSTATE_SERVER )
			SERVERCOMMANDS_SetThingState( self, STATE_IDLE );
		return;
	}

	angle_t ang = R_PointToAngle2(self->x, self->y, targ->x, targ->y);
	self->angle = ang;
	self->args[0]++;
	ang >>= ANGLETOFINESHIFT;
	if (!P_TryMove(self, self->x + 6 * finecosine[ang], self->y + 6 * finesine[ang], true))
	{
		self->SetIdle(true);
		if ( NETWORK_GetState() == NETSTATE_SERVER )
			SERVERCOMMANDS_SetThingState( self, STATE_IDLE );
		return;
	}
	if (self->args[0] & 2)
	{
		self->velx += (pr_fly() - 128) << BOBTOFINESHIFT;
		self->vely += (pr_fly() - 128) << BOBTOFINESHIFT;
	}
	int zrand = pr_fly();
	if (targ->z + 5*FRACUNIT < self->z && zrand > 150)
	{
		zrand = -zrand;
	}
	self->velz = zrand << BOBTOFINESHIFT;
	if (pr_fly() < 40)
	{
		// [rc4l] true: inform the clients, as A_BatMove's scream does.
		S_Sound(self, CHAN_VOICE, self->ActiveSound, 0.5f, ATTN_STATIC, true);
	}

	// [rc4l] Push the new position, velocity and angle out to the clients.
	if ( NETWORK_GetState() == NETSTATE_SERVER )
	{
		SERVERCOMMANDS_MoveThingExact( self, CM_X|CM_Y|CM_Z|CM_VELX|CM_VELY|CM_VELZ|CM_ANGLE );
	}
}
