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

}} // namespace zx::netreplication

#endif // ZX_ACTORSTATE_H
