// [MGOOOOOO] The one-tic actors' game half (hitboxviz.h NoteDestroyed): what a tic reads of an actor it destroys, handed to the overlay as a plain record.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

#include "actor.h"
#include "doomstat.h"   // gametic
#include "g_level.h"    // level.time

#include "features/hitboxviz/hitboxviz.h"
#include "features/hitboxviz/computation/ticboxes_compute.h"
#include "features/net-session/netrole.h"   // IsServer

namespace zx { namespace hitboxviz {

void NoteDestroyed(const AActor *thing)
{
	if (zx::net::IsServer())   // [MGOOOOOO] a server draws nothing
		return;
	if (!ComputeTicBoxWanted(thing->fua_spawnTic, gametic, !!(thing->flags & MF_MISSILE), !!(thing->flags & MF_SOLID),
		!!(thing->flags & MF_SHOOTABLE), !!(thing->flags5 & MF5_NOINTERACTION)))
		return;

	TicBoxRecord box;
	box.prevX = FIXED2FLOAT(thing->PrevX);
	box.prevY = FIXED2FLOAT(thing->PrevY);
	box.prevZ = FIXED2FLOAT(thing->PrevZ);
	box.x = FIXED2FLOAT(thing->x);
	box.y = FIXED2FLOAT(thing->y);
	box.z = FIXED2FLOAT(thing->z);
	box.radius = FIXED2FLOAT(thing->radius);
	box.height = FIXED2FLOAT(thing->height);
	box.attackRadius = FIXED2FLOAT(thing->GetAttackRadius());
	box.attackHeight = FIXED2FLOAT(thing->GetAttackHeight());
	box.missile = !!(thing->flags & MF_MISSILE);
	box.expiryTic = ComputeTicBoxExpiry(level.time);
	PushTicBox(box);
}

}} // namespace zx::hitboxviz
