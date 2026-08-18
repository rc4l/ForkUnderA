// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] PROJECTED MESH DECALS -- a mark is printed onto real geometry, not glued to one sidedef.
//
// At the moment of impact a box is built around the contact point, oriented along the direction the
// projectile was travelling. Everything inside that box -- wall segments, floors, ceilings, the far
// side of a corner -- is clipped against it, and the pieces that come back are the decal. They are
// ordinary triangles with ordinary texture coordinates, drawn by the same path that draws every
// other decal quad.
//
// What that buys, over the glued quad Doom has always used:
//
//   * A mark that runs onto the floor under it, or round a corner, is ONE projection covering both
//     surfaces. Nothing has to decide where the mark carries over, so there is no seam to get
//     wrong -- the two pieces end at the same place because they were cut by the same box.
//   * The angle of the shot is in the picture. A rocket arriving at a slant leaves a slanted mark.
//   * The blocking line does not have to be the visible one. Doom stops a missile when its bounding
//     BOX touches a line, so a shot into a corner is often stopped by a line that carries no
//     texture, and the engine then makes no decal at all -- the reported "no decal when hitting
//     connecting lines". A box does not care which line stopped the projectile; it prints on
//     whatever geometry is inside it.
//
// The arithmetic lives in computation/decalproject_compute.h, where it is tested. This file is the
// glue: gathering candidate surfaces from the map, and re-emitting the clipped result each frame.

#ifndef ZX_PROJDECALS_H
#define ZX_PROJDECALS_H

#include "doomtype.h"

class DBaseDecal;
class FDecalTemplate;
struct line_t;

namespace zx { namespace levelmesh {

// [rc4l] What the projectile was doing when it stopped, handed down from where that is still known.
//
// P_ExplodeMissile zeroes the velocity long before the decal is created, and the decal code has no
// way back to the missile, so the direction has to be carried across. Set it around the spawn and
// clear it afterwards; an unset context means "no direction available", which is a real case (a
// hitscan puff, a decal placed by a script) and produces a head-on projection.
void SetImpactContext(fixed_t velX, fixed_t velY, fixed_t velZ, fixed_t radius);
void ClearImpactContext();

// [rc4l] Build the projection for a decal the engine has just created.
//
// Called with the engine's own decal so that the two stay in step: the engine's thinker owns the
// fade, the lifetime and the `cl_maxdecals` recycling, and this reads the alpha off it every frame
// rather than reimplementing any of that. When the engine destroys the decal, ForgetProjectedDecal
// takes the projection with it.
void SpawnProjectedDecal(DBaseDecal *owner, const FDecalTemplate *tpl,
                         fixed_t x, fixed_t y, fixed_t z, line_t *hitLine);

// [rc4l] A mark where the engine makes none at all.
//
// Doom only decals WALLS: P_ExplodeMissile spawns one when the missile died against a line, and a
// rocket that lands on the ground leaves nothing. There is no engine decal to own such a mark, so
// this one owns itself -- it carries the template's alpha and, if the template fades, the fader's
// own timing, read once at spawn from the animator that would have run.
void SpawnProjectedDecalHere(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                             const float surfaceNormal[3]);

// [rc4l] A mark on a wall the engine REFUSED to decal.
//
// DBaseDecal::StickToWall has to pick a texture for the mark to live on, and when the hit lands in
// the open span of a two-sided line there is none -- so it returns a null texture id and vanilla
// makes no decal at all. That is the reported "no decal when hitting connecting lines": aim at the
// seam between two linedefs and the shot leaves nothing.
//
// A projection does not need a texture to live on, only geometry to be cut from, and that is still
// there. Same mark, no owner, so it fades from the template's own animator.
void SpawnProjectedDecalOnLine(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                               line_t *hitLine);

// The engine is destroying this decal -- drop whatever was projected for it.
void ForgetProjectedDecal(DBaseDecal *owner);

// [rc4l] Emit this frame's projected decals into the dynamic mesh. Called from CreateScene, beside
// the sprites, because like sprites they are re-emitted every frame rather than baked.
void RegisterProjectedDecals();

// Everything, on level change: the geometry these were clipped against is about to stop existing.
void ClearProjectedDecals();

// How many projections are live, and how many triangles they came to. See fua_projdecals_stats.
void GetProjectedDecalStats(int &decals, int &triangles);

}} // namespace zx::levelmesh

#endif // ZX_PROJDECALS_H
