// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] features/decals: every decal in the level is made here, walls, floors and ceilings alike, under the one rule in computation/markrules_compute; the decal classes befriend its Maker, so nothing else can make one.

#ifndef ZX_DECALS_H
#define ZX_DECALS_H

#include "doomtype.h"
#include "name.h"
#include "tables.h"   // [rc4l] angle_t
#include "textures/textures.h"   // [rc4l] FTextureID
#include "features/decals/computation/markrules_compute.h"

class AActor;
class DBaseDecal;
class FDecalTemplate;
struct side_t;
struct line_t;
struct sector_t;
struct F3DFloor;
struct FTraceResults;

namespace zx { namespace decals {

// [rc4l] A hitscan's or a rail's (source) mark where its trace stopped, on a wall, a floor or a ceiling, from the shooter's weapon's generator or the actor's own, as P_LineAttack chose the actor.
void MarkShot(AActor *shooter, const FTraceResults &trace, int source);

// [rc4l] A missile's mark as P_ExplodeMissile lets it go: on the line it struck, else on the floor or ceiling it came to rest against with no target; vel is how it was moving before it stopped.
void MarkMissile(AActor *missile, line_t *line, AActor *target, fixed_t velx, fixed_t vely, fixed_t velz);

// [rc4l] Blood a hit sprays where its trace reached, travelling (vx, vy, vz), shaded the victim's colour.
void MarkBlood(const char *bloodType, const FTraceResults &trace, fixed_t vx, fixed_t vy, fixed_t vz, PalEntry color);

// [rc4l] A player's spray, on the wall its trace reached.
void MarkSpray(const char *name, const FTraceResults &trace);

// [rc4l] ShootDecal: a decal a map thing (source kSourcePlaced) or a script places, traced from (x, y, z) along angle to a wall; a permanent one is never recycled. Returns it, or NULL.
DBaseDecal *ShootMark(const FDecalTemplate *tpl, AActor *basis, sector_t *sec, fixed_t x, fixed_t y, fixed_t z, angle_t angle, fixed_t tracedist, bool permanent, int source);

// [rc4l] Whether a wall a decal spreads onto takes its clone: the surface's own refusals, a permanent decal's clone ignoring the side's opt-out as stock's does.
bool WallAcceptsClone(side_t *wall, fixed_t x, fixed_t y, fixed_t z, F3DFloor *ffloor, bool permanent);

// [rc4l] Whether a level surface of this base texture refuses marks however they are made, a wall by its side's flags too, for the world pass's per-pixel bit.
bool SurfaceRefusesMarks(FTextureID texture, bool wall, DWORD sideFlags);

// [rc4l] Clears what fua_decal_report has counted, as a level begins.
void ResetDecalReport();

}} // namespace zx::decals

#endif // ZX_DECALS_H
