// [Doruk-Aksoy] Named random streams for ACS (fua_Random 24070, fua_FRandom 24071, fua_SetRandomSeed 24072): each name is an engine random stream of
// its own, apart from the one Random() draws on, seeded from the game's rngseed and kept in savegames and demos.

#ifndef ZX_ACSRANDOM_H
#define ZX_ACSRANDOM_H

#include <cstdio>

struct PNGHandle;

namespace zx { namespace acsrandom {

// [Doruk-Aksoy] A whole number from `min` to `max` from stream `name`, CLIENTSIDE scripts' streams apart; 0 for a name no stream can have.
int Random(const char *name, bool clientside, int min, int max);

// [Doruk-Aksoy] A fixed-point number from `min` to `max` from stream `name`; 0 for a name no stream can have.
int FixedRandom(const char *name, bool clientside, int min, int max);

// [Doruk-Aksoy] Starts stream `name` over from `seed`, as ZScript's SetRandomSeed does; 1 once done, 0 for a name no stream can have.
int SetSeed(const char *name, bool clientside, int seed);

// [Doruk-Aksoy] The savegame chunk naming every stream a script made.
void WriteStreams(FILE *file);

// [Doruk-Aksoy] Makes the streams a savegame names, before FRandom::StaticReadRNGState restores their state, which skips a stream that doesn't exist yet.
void ReadStreams(PNGHandle *png);

}} // namespace zx::acsrandom

#endif
