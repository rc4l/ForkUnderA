// [MGOOOOOO] The overlay's one-tic actors, spawned and destroyed in the same tic (a weapon's one-tic hitbox) so no copy between tics holds them, kept as events for that tic's frames.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_HITBOXVIZ_TICBOXES_COMPUTE_H
#define ZX_HITBOXVIZ_TICBOXES_COMPUTE_H

#include "features/hitboxviz/computation/recordstore_compute.h"

namespace zx { namespace hitboxviz {

// [MGOOOOOO] One gone actor's boxes: where its tic began and where it went, so the frames interpolate it as they do a live actor.
struct TicBoxRecord
{
	float prevX, prevY, prevZ;
	float x, y, z;
	float radius, height;
	float attackRadius, attackHeight;
	bool missile;
	int expiryTic;
};

enum
{
	// [MGOOOOOO] A crowded tic's one-tic actors past this many are unreadable anyway, so the oldest go first.
	MAX_TIC_BOX_RECORDS = 256,
};

typedef RecordStore<TicBoxRecord, MAX_TIC_BOX_RECORDS> TicBoxStore;

// [MGOOOOOO] Whether an actor going in tic nowTic is one no copy ever held (spawned in that same tic) that can touch something: a missile, solid or shootable, never NOINTERACTION.
bool ComputeTicBoxWanted(int spawnTic, int nowTic, bool missile, bool solid, bool shootable, bool noInteraction);

// [MGOOOOOO] The expiry of a record made while the level time was madeTime: the copy of that tic's end, whose level time is one more (P_Ticker's level.time++), shows it, the next drops it, and a pause stops the level time with it.
int ComputeTicBoxExpiry(int madeTime);

// [MGOOOOOO] Whether the frames of a copy whose level time is copyTime show a record expiring at expiryTic.
bool ComputeTicBoxShown(int expiryTic, int copyTime);

}} // namespace zx::hitboxviz

#endif // ZX_HITBOXVIZ_TICBOXES_COMPUTE_H
