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
struct F3DFloor;

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
