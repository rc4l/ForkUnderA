// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Bots that survive a savegame.
//
// See computation/botsave_compute.h for why this is a chunk and not a change to the save stream,
// and for what is deliberately left out of it.

#ifndef ZX_BOTSAVE_H
#define ZX_BOTSAVE_H

#include <stdio.h>

struct PNGHandle;

namespace zx
{

// Append the roster to a save being written. Writes nothing when there are no bots, which is what
// every save before this looked like.
void BotSave_Write( FILE *file );

// Re-occupy the slots the bots held, BEFORE the player matcher runs, so the data already in the
// save has somewhere to land. Silent and harmless when the chunk is absent or does not parse: no
// bots is a state the loader already handles correctly, and a bad chunk must never fail a load.
void BotSave_Restore( PNGHandle *png, FILE *file );

} // namespace zx

#endif // ZX_BOTSAVE_H
