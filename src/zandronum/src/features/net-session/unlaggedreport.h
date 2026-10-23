// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_unlagged_report: each hitscan a player fires, as the server's unlagged rewound the others for it and as the shooter's own client found them, so a scenario can measure how far apart the two are.

#ifndef ZX_UNLAGGEDREPORT_H
#define ZX_UNLAGGEDREPORT_H

class AActor;

namespace zx { namespace netsession {

// [rc4l] A server's hitscan by a player, its trace just run with the others rewound: where it met each, where each really was, and whom it hit.
void NoteServerShot(AActor *shooter, AActor *hit);

// [rc4l] A client's own hitscan, with where it found the other players.
void NoteClientShot(AActor *shooter);

// [rc4l] A client's own trace of the shot NoteClientShot logged, which it runs only to predict puffs (cl_clientsidepuffs), and the actor it hit.
void NoteClientTrace(AActor *shooter, AActor *hit);

}} // namespace zx::netsession

#endif // ZX_UNLAGGEDREPORT_H
