//-----------------------------------------------------------------------------
//
// [ForkUnderA] cl_fua_caching -- level-load asset precache
//
// Modes: 0 = off (legacy lazy loading, byte-identical to stock behavior),
//        1 = every state of every placed actor class,
//        2 = mode 1 plus the transitive spawn closure (classes reachable
//            through constant DECORATE state parameters, drop items and
//            blood types), the default.
//
// Everything here is client/render/sound side only: it runs after
// P_SetupLevel has spawned things, is skipped on dedicated servers and
// during demo playback by its callers, and never touches sim state.
//
//-----------------------------------------------------------------------------

#ifndef __FUA_CACHING_H__
#define __FUA_CACHING_H__

#include "doomtype.h"

// The clamped cl_fua_caching value (0..2).
int FUA_CachingMode ();

// Sets spritelist[sprite] = 1 for every sprite of every class in the cached
// set. Called from DFrameBuffer::GetHitlist; no-op in mode 0.
void FUA_MarkCachedSprites (BYTE *spritelist, unsigned int numsprites);

// Marks property sounds and constant state-parameter sounds of the cached
// set as used. Called from S_PrecacheLevel before its cache/unload loops;
// no-op in mode 0.
void FUA_MarkCachedSounds ();

#endif
