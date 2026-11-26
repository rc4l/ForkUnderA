// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] net-replication's public header for actors: what a server sends of one that changed.

#ifndef ZX_ACTORSTATE_H
#define ZX_ACTORSTATE_H

class AActor;
struct FState;

namespace zx { namespace netreplication {

// [rc4l] An actor took damage and lived, on a server: its health goes to every client at the tic's end (ComputeSendsWoundedHealth).
void NoteDamaged(AActor *target);

// [rc4l] Each actor wounded this tic and still alive sends its health once, from SendChanges.
void SendWoundedHealth();

// [rc4l] An actor whose floorclip changed after its spawn went out, on a server: the clip to every client (SVC2_SETTHINGFLOORCLIP).
void SendFloorClip(AActor *actor);

// [rc4l] An actor property a script changed, on a server: the new value to every client (SVC_SETTHINGPROPERTY).
void SendThingProperty(AActor *actor, int property);

// [Doruk-Aksoy] An actor whose translation a state changed (A_SetTranslation), on a server: the new one to every client (SVC_SETTHINGTRANSLATION).
void SendThingTranslation(AActor *actor);

// [Doruk-Aksoy] An actor a server put in a state of its class's own (a Slam state), on a server: the state to every client (SVC_SETTHINGFRAME).
void SendThingFrame(AActor *actor, FState *state);

// [Doruk-Aksoy] A mover P_XYMovement stopped dead against a block, on a server: its place and velocity exactly to every client when it was moving (ComputeSendsBlockedStop).
void SendBlockedStop(AActor *actor, bool wasMoving);

// [rc4l] A puff a server spawned, to its clients in state (STATE_SPAWN or the one it jumped to) as ComputePuffSend says, flags its P_SpawnPuff flags.
void SendPuff(AActor *puff, AActor *source, unsigned state, int flags);

}} // namespace zx::netreplication

#endif // ZX_ACTORSTATE_H
