// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_friendlymonsters.h for why this exists rather than `notarget`.

#include "zx_friendlymonsters.h"

#include "doomtype.h"
#include "doomstat.h"
#include "actor.h"
#include "p_local.h"
#include "c_cvars.h"
#include "network.h"
#include "r_defs.h"
#include "r_state.h"

namespace
{

// Put one monster on, or off, the player's side.
//
// Only things that can actually fight: MF_COUNTKILL alone misses bosses and friendly-by-design
// actors, so MF3_ISMONSTER is the test, and players/corpses are skipped outright.
void ApplyTo( AActor *mo, bool friendly )
{
	if (( mo == NULL ) || ( mo->player != NULL ))
		return;
	if (( mo->flags3 & MF3_ISMONSTER ) == 0 )
		return;

	// [rc4l] Server decides, client is told. This rewrites AI state (side, target, dormancy), which
	// under client/server belongs to the server alone: a client applying it locally would be
	// predicting a change the server may not have made yet. The cvar is CVAR_SERVERINFO, so a client
	// still knows the value, it just does not act on it. Single player is not client mode, so the
	// dev use this exists for is unaffected.
	if ( NETWORK_InClientMode( ))
		return;

	if ( friendly )
	{
		// A monster that was ALREADY friendly (a Strife ally, a mapper's own) is left completely
		// alone: it was never hostile to the player, and rewriting it would mean turning the cvar
		// back off makes a level's own allies hostile. Skipping it entirely is also what lets one
		// mark bit be exact -- marked means "this feature changed both its side AND its dormancy",
		// so the restore has nothing to guess.
		if ( mo->flags & MF_FRIENDLY )
			return;

		mo->STFlags |= STFL_FUA_WASHOSTILE;
		mo->flags |= MF_FRIENDLY;

		// The flag governs what it will target NEXT. Anything already locked onto the player keeps
		// chasing and firing until that pointer is cleared, which is the same gap that makes
		// `notarget` alone insufficient.
		mo->target = NULL;
		mo->lastenemy = NULL;
		mo->LastHeard = NULL;

		// And friendly is not the same as quiet. A friendly monster still hunts, it just hunts the
		// OTHER monsters: it wakes, plays its see-sound and walks off to find a fight. Standing in
		// a level watching it come alive around you is not what this cvar is for.
		//
		// Deactivate is the engine's own answer: MF2_DORMANT plus the Inactive state, or tics = -1
		// when the class has none, which stops the state machine outright so A_Look never runs
		// again. No looking means no see-sound and no wandering.
		mo->Deactivate( NULL );
	}
	else if ( mo->STFlags & STFL_FUA_WASHOSTILE )
	{
		mo->STFlags &= ~STFL_FUA_WASHOSTILE;
		mo->flags &= ~MF_FRIENDLY;
		mo->target = NULL;
		mo->lastenemy = NULL;
		mo->Activate( NULL );
	}
}

void ApplyToLevel( bool friendly )
{
	if ( gamestate != GS_LEVEL )
		return;

	TThinkerIterator<AActor> it;
	AActor *mo;
	while (( mo = it.Next() ) != NULL )
		ApplyTo( mo, friendly );

	// A sector remembers who last made noise in it and A_Look reads that back, so a stale entry
	// re-alerts everything the moment one of them looks again.
	if ( friendly )
	{
		for ( int i = 0; i < numsectors; ++i )
			sectors[i].SoundTarget = NULL;
	}
}

} // namespace

// CVAR_SERVERINFO because it changes what the simulation does, so a client must not be able to
// disagree with the server about it. Not archived: this is for looking at a level, and it would be
// a nasty surprise to find it still on in a real game days later.
CUSTOM_CVAR( Bool, sv_fua_friendlymonsters, false, CVAR_SERVERINFO | CVAR_NOSETBYACS )
{
	ApplyToLevel( !!self );
}

namespace zx
{

void FriendlyMonsters_Spawned( AActor *mo )
{
	if ( sv_fua_friendlymonsters )
		ApplyTo( mo, true );
}

void FriendlyMonsters_Loaded( AActor *mo )
{
	if (( mo == NULL ) || (( mo->STFlags & STFL_FUA_WASHOSTILE ) == 0 ))
		return;
	if ( sv_fua_friendlymonsters )
		return; // Save and cvar agree; leave it pacified.

	// Undone by hand rather than through Activate(), which calls SetState during deserialisation,
	// when the actor is not yet fully linked. Clearing dormancy and giving the state machine a tic
	// is enough: it resumes from whatever state it was frozen in on the next run.
	mo->STFlags &= ~STFL_FUA_WASHOSTILE;
	mo->flags &= ~MF_FRIENDLY;
	mo->flags2 &= ~MF2_DORMANT;
	if ( mo->tics == -1 )
		mo->tics = 1;
}

} // namespace zx
