// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Decals on FLOORS and CEILINGS, which the engine has never had.
//
// A Doom decal is a wall object: DBaseDecal hangs off side_t::AttachedDecals, is positioned along a
// linedef, and P_LineAttack only spawns one when the trace reports TRACE_HitWall. Shoot the floor
// and nothing is marked. That is an engine limitation rather than a renderer one -- GL would draw
// plane decals perfectly well if any existed -- so this adds the missing half rather than a second
// way to draw the existing one.
//
// Kept entirely outside DBaseDecal on purpose. That class carries a linedef, a wall offset, an
// animator and a place in the thinker list, none of which mean anything on a plane, and inheriting
// from it would mean special-casing all four. These are a small fixed ring of records with a
// position, a plane and a texture.
//
// They are emitted through the same dynamic decal path the wall decals use -- depth bias, alpha
// masks, back-to-front sorting -- so nothing about the drawing is new.

#ifndef ZX_FLATDECALS_H
#define ZX_FLATDECALS_H

#include "doomtype.h"
#include "textures/textures.h"

class FDecalTemplate;
class DBaseDecal;
struct F3DFloor;
struct side_t;

namespace zx { namespace levelmesh {

// Mark a floor or ceiling at (x, y) on the plane through z. `ceiling` says which way it faces, and
// decides both the winding and which way the quad is nudged off the plane.
//
// `rover` is the 3D floor the shot landed on, or NULL for the sector's own plane. It matters because
// a decal has to RIDE the surface it is stuck to, and a 3D floor moves independently of the sector
// that contains it -- tracking the sector's floor instead would leave a decal hanging in the air the
// moment the 3D floor it is painted on moves.
// `shadeOverride` replaces the template's own colour when non-zero -- blood decals are shaded with
// the bleeding actor's blood colour rather than with anything in the template.
// `isLower` marks the decal a template names to sit UNDERNEATH it. It gets less clearance from the
// surface so the pair has a settled order instead of two coplanar quads trading places every frame.
void SpawnFlatDecal(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z, bool ceiling,
                    F3DFloor *rover, DWORD shadeOverride = 0, bool isLower = false);

// Emit this frame's flat decals into the dynamic mesh. Called once per frame beside the sprites.
void RegisterFlatDecals();

// Dropped when the level changes: the records hold plane heights that mean nothing in a new map.
void ClearFlatDecals();

int FlatDecalCount();

// [rc4l] A decal as a VOLUME rather than a quad, for the projected pass.
//
// A quad glued to a surface has to be positioned on it, nudged off it, biased in the depth test and
// ordered against everything nearby -- four separate ways to be subtly wrong, all of which have
// been. A projected decal instead describes a box in the world and lets the shader paint whatever
// surface it finds inside, read from the depth buffer. Nothing is glued to anything, so none of
// those four questions exist.
struct ProjectedDecal
{
	// [rc4l] The box carries an ORIENTATION, so a wall is the same case as a floor.
	//
	// A floor decal's box is axis-aligned; a wall's is turned to face the wall. Once the box knows
	// which way it points, one pass draws both -- and that is worth doing, because the two problems
	// left on wall decals are exactly the two a glued quad can't escape. They layer wrongly against
	// the wall's own glow, and they are cut off at a linedef join, because a DBaseDecal is clipped to
	// the sidedef it hangs on and the engine papers over it by cloning slices onto the neighbours. A
	// projected box has no sidedef to be clipped to, so it simply carries on across the join.
	//
	// The axes arrive ALREADY divided by their half-extents, so a dot product against each lands in
	// -1..1 across the box and the shader needs no division at all.
	float x, y, z;             // centre, on the surface, in MAP space (x, y, z-up)
	float ux, uy, uz;          // across the surface
	float vx, vy, vz;          // up the surface
	float nx, ny, nz;          // through the surface
	float halfW, halfH, halfDepth;   // the same three extents, unscaled, to build the box corners
	// [rc4l] Which SLICE of the picture this box owns, in local-y units.
	//
	// A mark near a corner is drawn by several boxes -- itself, plus a fold for each surface it runs
	// into -- and they have to tile, not overlap. Two things go wrong without that. A fold's box
	// straddles the wall it folded from, because its centre goes back inside the wall to make the
	// coordinate line up, so it would also paint the surface on the near side and give it the wrong
	// part of the graphic. And the mark itself would carry on past the join that the fold continues
	// from, so both paint the strip either side of the corner and the double blend shows as a seam
	// running along it.
	//
	// So each box states the range of the picture it is responsible for, and the joins are where one
	// hands over to the next. -1 to 1 is the whole picture, which is a mark with nothing to fold onto.
	float vMin, vMax;
	const void *material;      // FMaterial*
	float r, g, b, a;          // colour the shader paints, alpha already faded
	bool  additive;
	bool  redToAlpha;          // the texture is an alpha mask, not a colour image
};

// This frame's projected decals. Rebuilt by RegisterFlatDecals.
int GetProjectedDecals(const ProjectedDecal **out);

// [rc4l] Mirror a WALL decal into the projected pass.
//
// Called once the engine has finished building its own DImpactDecal, so this inherits every decision
// it already made -- which template won, whether the wall accepts decals at all, the shade a bleeding
// actor asked for, the final scale. Reading them off the finished object beats re-deriving them and
// beats a per-source hook: hitscans, missiles, blood, rail, ACS and the network all end up here.
//
// The engine's own spread clones are deliberately NOT mirrored. They exist to carry a decal past the
// edge of a sidedef, which is the very thing the projected box does for free.
void SpawnWallDecal(const DBaseDecal *decal, const side_t *wall, const FDecalTemplate *tpl);

// Dropped with the level, like the flat ring.
void ClearWallDecals();

// Print the live wall decals: resolved height, anchor offset, size, box depth. See fua_walldecals.
void DumpWallDecals();

// Spawn/emit counters, split by cause. See fua_flatdecals.
// What the trace reported at the point decals are decided, so "the branch never ran" and "it ran
// and the hit was a wall" stop being the same observation.
void NoteImpact(int hitType, bool noImpactFlag, bool noDecalFlag);

// Why a MISSILE impact did or did not mark a plane. Missiles reach P_ExplodeMissile, not
// P_LineAttack, which is why plasma marked walls but never floors.
void NoteMissileImpact(bool hasGenerator, bool onFloor, bool onCeiling);
extern int g_missileTries, g_missileNoGen, g_missileNotPlane;
extern int g_impactWall, g_impactFloor, g_impactCeiling, g_impactOther, g_impactSuppressed;
extern float g_lastHitZ, g_lastPlaneSpawn, g_lastPlaneNow;
extern bool g_lastRover;
extern float g_lastX, g_lastY, g_lastZ, g_lastHW, g_lastHH, g_lastAlpha;
extern bool g_lastRed;
extern unsigned int g_lastShade;
extern int g_lastLight;
extern int g_spawnTries, g_spawnNoTemplate, g_spawnNoTexture, g_spawnNoSector,
           g_spawnNoSurface, g_emitted;

}} // namespace zx::levelmesh

#endif // ZX_FLATDECALS_H
