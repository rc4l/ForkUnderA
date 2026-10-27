// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLDEFS for every renderer, moved from gl/dynlights/gl_dynlight.cpp: the lumps' parser (lights, glows, sky boxes, brightmaps, hardware shaders) and the lights it attaches to actors.

#ifndef ZX_GLDEFS_H
#define ZX_GLDEFS_H

class AActor;

// [rc4l] Read the game's *DEFS lump and every GLDEFS, then attach their lights to the actor classes.
void gl_ParseDefs();

// [Doruk-Aksoy] The game's own *DEFS lump read before GLDEFS, or null for a game with none.
const char *gl_GameDefsLump();

// [rc4l] Attach the lights an actor's sprite frame or state names, putting the rest to sleep.
void gl_SetActorLights(AActor *actor);

// [rc4l] Drop every actor's attached lights (before a save, or with r_lights off), and attach them again.
void gl_DeleteAllAttachedLights();
void gl_RecreateAllAttachedLights();

#endif // ZX_GLDEFS_H
