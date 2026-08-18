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

// [rc4l] Drop every remembered decal, because the level they belong to is going away.
//
// A wall decal keeps a side_t * so it can follow a door that moves -- see WallDecal::wall -- and
// those point into geometry P_SetupLevel is about to free. Keeping them across a level change is a
// dangling pointer, not a stale position: the next frame's decal pass dereferences it and the engine
// dies inside RealZOnWall.
void ForgetDecals();

// [rc4l] A mark for an impact the engine refused to attach to a sidedef, because the hit landed in
// the open span of a two-sided line and there was no texture there to glue a quad to. A projected
// decal needs no sidedef -- see the note on the definition.
void SpawnUnstuckWallDecal(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                           const side_t *wall);

// [rc4l] Which way the projectile was travelling when it hit, set around the decal spawn.
//
// The blocking line is not always the surface you can see: a projectile has a radius, so it can be
// stopped by one face of a corner while the face it visibly struck is the neighbour. When the
// blocking line has no texture to hold a mark, this direction is what lets the mark be placed on
// the surface actually hit rather than abandoned. Zero means "not known", and nothing is traced.
void SetImpactDirection(fixed_t dx, fixed_t dy);

// [rc4l] What a missile died against: a blocking line, an actor, or neither. Only the first can mark
// a wall, so a "dead zone" where a projectile leaves nothing is one of the other two.
void NoteMissileDeath(bool hasLine, bool hasTarget, bool hasGenerator, double z, double floorZ);

// Dropped when the level changes: the records hold plane heights that mean nothing in a new map.
void ClearFlatDecals();

int FlatDecalCount();

// [rc4l] A mark as a BLAST, not as a quad and not as a projection from a plane.
//
// A quad glued to a surface has to be positioned on it, nudged off it, biased in the depth test and
// ordered against everything nearby -- four ways to be subtly wrong, all of which were. It is also
// stuck to one sidedef, so it stops at a linedef join and at the foot of a wall, and the engine has
// no floor decals for it to continue onto.
//
// This describes where a blast landed and how far it reached. The backend draws a box around it,
// finds the real surfaces inside from the depth buffer and their exact orientation from the
// G-buffer, and measures each one IN ITS OWN PLANE from the centre. That is what makes a corner
// unremarkable: every surface in range is parameterised at the mark's true scale, whatever angle it
// sits at, so there is no such thing as a surface the mark fails to cover or stretches across.
//
// Projecting along a fixed axis instead -- which is what this was for most of a day -- degenerates on
// any surface running along that axis, and no amount of patching fixes that. It produced, in order: a
// dragged row of texels, a black slab where the drag covered a whole box, a hole where the slab was
// refused, and a wedge of floor that the strip patching the corner could never reach.
struct ProjectedDecal
{
	// The picture's own axes, ALREADY divided by their half-extents, so laying them into a surface is
	// a bare dot product with nothing left to scale. They no longer describe a box -- only which way
	// up the picture goes once it lands on something.
	float x, y, z;             // where the blast landed, in MAP space (x, y, z-up)
	// [rc4l] Where the wall this landed on ENDS, either side of the blast, along the picture's
	// across-axis. Without it the shader unfolds about an edge it assumes is infinite; in the map
	// that edge stops at a corner, and a path crossing it beyond the end does not exist. Zero span
	// means unknown, and the plain single hinge stands.
	float alongMin, alongMax;
	float ux, uy, uz;          // across the picture
	float vx, vy, vz;          // up the picture
	float nx, ny, nz;          // out of the surface it was fired at
	// [rc4l] How far the mark reaches, in every direction from where it landed.
	//
	// A blast does not project from a plane, it radiates from a POINT, and that is the whole of the
	// difference. Projecting from a plane means asking every surface to be parameterised by an axis
	// chosen before the surface was known, which works on the surface that was hit and degenerates on
	// everything else -- a floor met at a right angle gets one row of texels dragged across it,
	// because the projection has no way to express movement along its own axis. Every attempt to
	// paper over that failed in a different place: dragged rows, then a black slab where the drag
	// covered the whole box, then a hole where the slab was refused, then a wedge of floor that the
	// strip patching it round the corner never reached.
	//
	// Radiating from the point has none of those. Each surface inside the radius is measured in ITS
	// OWN plane from the blast's centre, so each gets a mapping at the mark's true scale with nothing
	// degenerate about it, and there is no such thing as a surface the mark fails to cover -- if it is
	// in range it is parameterised, whatever angle it sits at. Corners come out looking like soot
	// that travelled, because that is what the arithmetic is describing.
	float radius;
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
// edge of a sidedef, and a mark measured from where it landed has no sidedef to be stopped at.
void SpawnWallDecal(const DBaseDecal *decal, const side_t *wall, const FDecalTemplate *tpl);

// Dropped with the level, like the flat ring.
void ClearWallDecals();

// Print the live wall decals: resolved height, anchor offset, size, reach, and what the pass drew
// last frame. See fua_walldecals.
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
