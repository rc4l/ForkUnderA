// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] PROJECTED DECALS -- a mark is a box in the world, resolved per fragment.
//
// At the moment of impact a box is built around the contact point, oriented along the direction the
// projectile was travelling. Nothing is cut and nothing is glued: the backend draws the box, reads
// the depth and normal the world has already written, and paints whatever surface is inside it.
//
// What that buys over the glued quad Doom has always used:
//
//   * A mark spanning a corner is ONE projection covering both faces, so there is no seam to get
//     wrong -- nothing has to decide where the mark carries over.
//   * The angle of the shot is in the picture. A projectile arriving at a slant leaves a slanted
//     mark, and how squarely it arrived fades it, per fragment.
//   * A rocket that lands on the GROUND leaves a scorch. Doom has never done this: a glued quad
//     needs a sidedef and a floor has none.
//   * The line that stopped the projectile does not have to be the one you can see. Doom stops a
//     missile when its bounding BOX touches a line, so a shot into a corner is often blocked by a
//     neighbour that carries no texture, and the engine then makes no decal at all -- the reported
//     "no decal when hitting connecting lines".
//
// The arithmetic lives in computation/decalproject_compute.h, where it is tested. This file keeps
// the marks and ages them; features/hwrender/diligent/dgdecals.cpp draws them.

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
// [rc4l] Is the projected-decal machinery doing anything this frame?
//
// Two switches reach it and they are not the same question. fua_decalmode turns projection on for
// WALLS, where it replaces the glued quad and brings that trade with it. fua_decal_flats turns it
// on for FLOORS AND CEILINGS, where there is nothing to replace -- Doom never marked a floor at
// all, because a glued quad needs a sidedef to be glued to and a floor has none.
//
// Everything downstream -- the store, the update, the GPU list, the deferred pass -- runs if
// EITHER is on, so it is asked here once rather than spelled out at each of the four places that
// need it. Those four had already been written out by hand, and a count written by hand is how the
// mirror PSO lost a variable.
bool ProjectedDecalsActive();

void SetImpactContext(fixed_t velX, fixed_t velY, fixed_t velZ, fixed_t radius);
void ClearImpactContext();

// [rc4l] Build the projection for a decal the engine has just created.
//
// Called with the engine's own decal so the two stay in step: the engine's thinker owns the fade,
// the lifetime and the cl_maxdecals recycling, and this reads the alpha off it every frame rather
// than reimplementing any of it. ForgetProjectedDecal takes the projection when the decal goes.
void SpawnProjectedDecal(DBaseDecal *owner, const FDecalTemplate *tpl,
                         fixed_t x, fixed_t y, fixed_t z, line_t *hitLine);

// [rc4l] A mark where the engine makes none at all: on a floor or ceiling, which Doom never decals.
//
// No owner, so it fades from the template's own animator, and it walks the `lowerdecal` chain that
// StaticCreate would otherwise have walked -- without which a BFG leaves its glow with no scorch.
void SpawnProjectedDecalHere(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                             const float surfaceNormal[3]);

// [rc4l] A mark on a wall the engine REFUSED to decal.
//
// StickToWall has to pick a texture for the mark to live on, and when the hit lands in the open span
// of a two-sided line there is none -- so it returns a null texture id and vanilla makes no decal at
// all. A projection needs geometry, not a texture, and the geometry is still there.
void SpawnProjectedDecalOnLine(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                               line_t *hitLine);

// The engine is destroying this decal -- drop whatever was projected for it.
void ForgetProjectedDecal(DBaseDecal *owner);

// Age every mark by one frame and drop the ones that are over. Called once a frame from CreateScene,
// whatever the mode is: a mark's lifetime is not the renderer's business.
void UpdateProjectedDecals();

// Everything, on level change: these are positions in a map that is about to stop existing.
void ClearProjectedDecals();

// How many marks are live. See fua_projdecals_stats.
int GetProjectedDecalCount();

// [rc4l] One mark, as the BACKEND needs it: a box and what to paint in it.
//
// Everything here is in MESH space (x, z-up, y), because that is the space the backend draws in.
struct GpuDecal
{
	float centre[3];
	float right[3];      // the picture's axes, each divided by its own half-extent, so the box
	float up[3];         // test is |dot(rel, right)| <= 1
	float axis[3];       // unit, pointing the way the projectile went
	float halfW, halfH;  // in world units, for the run-out radius
	float near_, far_;   // how far the box reaches either side of the contact point
	float r, g, b, a;
	const void  *material;
	// [rc4l] What this mark is stuck to, so it can ride a floor that moves.
	//
	// A projection is cut from geometry rather than glued to a texture, which is what lets it mark a
	// floor at all -- and it is also why it does not follow one. Holding a fixed world height, a mark
	// on a lift stays behind as the lift rises. So the height is stored as an OFFSET from a named
	// sector plane instead, and the plane's current height is looked up when the mark is drawn.
	//
	// The lookup is on the GPU: the shader reads a small per-sector table rather than the CPU
	// rewriting every decal every frame. That keeps the cost proportional to the number of SECTORS
	// that moved rather than to the number of marks in the level, so the decal records stop changing
	// after they are made.
	//
	// anchorSector < 0 means no anchor: a mark on a wall, which has no plane to ride.
	int          anchorSector;
	int          anchorPlane;    // 0 floor, 1 ceiling
	float        anchorOffset;   // world height at spawn, minus that plane's height there
	bool         redToAlpha;
	bool         additive;
	bool         fullbright;
};

// [rc4l] This frame's marks, in the order they were made.
//
// Order is the picture where two marks overlap -- a scorch and the glow that belongs on top of it --
// and arrival order is the engine's own answer, because a template creates its LOWER decal first.
// Returns the count and points `out` at the array, which is rebuilt each frame and owned here.
int GetProjectedDecalsGpu(const GpuDecal **out);

}} // namespace zx::levelmesh

#endif // ZX_PROJDECALS_H
