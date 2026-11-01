// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A count that moves on whenever a thinker joins or leaves a thinker list or an actor a sector's, so the actor list and sector links a tic's end made are known still to hold at its handoff.
#ifndef ZX_GAMECOPY_ACTORLISTS_H
#define ZX_GAMECOPY_ACTORLISTS_H

namespace zx { namespace gamecopy {

extern unsigned int g_actorLists;
// [Doruk-Aksoy] A count that moves on only when a thinker joins or leaves a thinker list, which an actor's move does not, so a fill knows the actor list it holds still holds.
extern unsigned int g_thinkerLists;

// [rc4l] A thinker list or a sector's actor list changed.
inline void NoteActorLists() { g_actorLists++; }
// [Doruk-Aksoy] A thinker list changed, which is a change of the actor lists too.
inline void NoteThinkerLists() { g_thinkerLists++; g_actorLists++; }

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_ACTORLISTS_H
