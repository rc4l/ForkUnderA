// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy's actors: one value record per actor, taken between tics in thinker order, so the renderer draws actors without holding a live one.

#ifndef ZX_GAMECOPY_ACTORS_H
#define ZX_GAMECOPY_ACTORS_H

#include "actor.h"

namespace zx { namespace gamecopy {

// [rc4l] An actor as the renderer reads it, its members declared once in fields.h.
struct ActorRecord
{
#define ZX_ACTOR_FIELD(member) decltype(AActor::member) member;
#define ZX_ACTOR_DERIVED(type, member) type member;
#include "features/gamecopy/fields.h"
#undef ZX_ACTOR_FIELD
#undef ZX_ACTOR_DERIVED
};

// [rc4l] Fills a record from a live actor, for the copy and for GL's code that shares a helper with the backend.
void TakeActor(AActor &live, ActorRecord &out);

// [rc4l] The actors of the last fill, in thinker order.
const ActorRecord *Actors(int &count);
// [rc4l] Record n of the last fill, NULL for none.
const ActorRecord *Record(int n);
// [rc4l] A record's index in the last fill, -1 for one not in it.
int IndexOf(const ActorRecord *r);
// [rc4l] The record the last fill took of a live actor, NULL for none, the actor only named and its record looked up.
const ActorRecord *RecordOf(const AActor *actor);
// [rc4l] The live actor a record names, for an interface that takes one and looks its record up again; never read through.
inline AActor *LiveOf(const ActorRecord *r) { return r != NULL ? static_cast<AActor *>(const_cast<void *>(r->who)) : NULL; }
// [rc4l] The first record in a sector's actor list, as its thinglist orders them, -1 for none.
int FirstActorIn(const sector_t *sector);
// [rc4l] GetBobOffset for a record, ticfrac of the way through its tic.
fixed_t BobOffset(const ActorRecord &a, fixed_t ticfrac);

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_ACTORS_H
