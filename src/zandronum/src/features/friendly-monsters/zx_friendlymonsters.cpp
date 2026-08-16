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

	if ( friendly )
	{
		// Remember whether this one was ALREADY friendly (a Strife ally, a mapper's own friendly
		// monster). Turning the cvar back off must not make those hostile -- they were never ours
		// to change, and a level that ships with allies would be quietly rewritten by the restore.
		if (( mo->flags & MF_FRIENDLY ) == 0 )
			mo->STFlags |= STFL_FUA_WASHOSTILE;

		mo->flags |= MF_FRIENDLY;

		// The flag governs what it will target NEXT. Anything already locked onto the player keeps
		// chasing and firing until that pointer is cleared, which is the same gap that makes
		// `notarget` alone insufficient.
		if (( mo->target != NULL ) && ( mo->target->player != NULL ))
		{
			mo->target = NULL;
			mo->lastenemy = NULL;
			if ( mo->SpawnState != NULL )
				mo->SetState( mo->SpawnState );
		}
		mo->LastHeard = NULL;
	}
	else if ( mo->STFlags & STFL_FUA_WASHOSTILE )
	{
		mo->STFlags &= ~STFL_FUA_WASHOSTILE;
		mo->flags &= ~MF_FRIENDLY;
		mo->target = NULL;
		mo->lastenemy = NULL;
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

} // namespace zx
