// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] net-replication's public header for actors: what a server sends of one that changed.

#ifndef ZX_ACTORSTATE_H
#define ZX_ACTORSTATE_H

class AActor;

namespace zx { namespace netreplication {

// [rc4l] An actor took damage and lived, on a server: its health goes to every client at the tic's end (ComputeSendsWoundedHealth).
void NoteDamaged(AActor *target);

// [rc4l] Each actor wounded this tic and still alive sends its health once, from SendChanges.
void SendWoundedHealth();

// [rc4l] An actor whose floorclip changed after its spawn went out, on a server: the clip to every client (SVC2_SETTHINGFLOORCLIP).
void SendFloorClip(AActor *actor);

// [rc4l] An actor property a script changed, on a server: the new value to every client (SVC_SETTHINGPROPERTY).
void SendThingProperty(AActor *actor, int property);

// [rc4l] A puff a server spawned, to its clients in state (STATE_SPAWN or the one it jumped to) as ComputePuffSend says, flags its P_SpawnPuff flags.
void SendPuff(AActor *puff, AActor *source, unsigned state, int flags);

}} // namespace zx::netreplication

#endif // ZX_ACTORSTATE_H
