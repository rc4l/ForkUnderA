// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/flatdecals.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/computation/flatdecal_compute.h"

#include "r_defs.h"
#include "r_state.h"
#include "decallib.h"
#include "p_local.h"
#include "gl/data/gl_vertexbuffer.h"   // FFlatVertex
#include "gl/data/gl_data.h"           // getExtraLight
#include "gl/textures/gl_material.h"
#include "p_3dfloors.h"
#include "p_trace.h"                   // ETraceResult, for NoteImpact
#include "c_cvars.h"
#include "c_dispatch.h"
#include "c_console.h"

// [rc4l] Off would mean the engine behaves as it always has. On by default because a floor you have
// emptied a magazine into looking untouched is the odd behaviour, not the fix.
CVAR(Bool, fua_flat_decals, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
EXTERN_CVAR(Bool, gl_wallmesh)

namespace zx { namespace levelmesh {

// [rc4l] A fixed ring rather than a growing list.
//
// Decals accumulate for the whole level and nothing ever collects them; DImpactDecal solves that
// with a global cap and an age-out thinker. A ring is the same guarantee with no bookkeeping: the
// 257th decal overwrites the 1st, memory is constant, and the cost per frame is bounded no matter
// how long someone stands still holding the trigger.
static const int kMaxFlatDecals = 256;

struct FlatDecal
{
	float      x, y, z;
	bool       ceiling;
	FTextureID pic;
	float      scaleX, scaleY;
	float      alpha;
	DWORD      shadeColor;
	bool       redToAlpha;
	bool       additive;
	// [rc4l] The surface this decal is stuck to, which is NOT always the sector's own plane.
	//
	// A shot landing on a 3D floor must ride that 3D floor: it moves independently of the sector
	// containing it, so tracking the sector's plane would leave the decal hanging in the air the
	// moment a 3D-floor lift moved. NULL means the sector's own plane.
	F3DFloor  *rover;
	// [rc4l] The plane's height as the RENDER sees it, sampled on this decal's first frame.
	//
	// Not sampled at spawn. Measured on dbab02: the same P_PointInSector lookup answers 128 during
	// P_LineAttack and 0 during the frame -- the engine moves planes about while tracing 3D floor
	// collision -- so a height captured in the game tic is not comparable with one read in the
	// renderer, and subtracting them teleported every decal by the difference.
	float      planeZ;
	bool       planeSampled;
};

// The plane a decal is stuck to: its 3D floor's if it has one, otherwise its sector's.
static const secplane_t *DecalPlane(const sector_t *sec, F3DFloor *rover, bool ceiling)
{
	if (rover != NULL)
	{
		// [rc4l] See ComputeDecalUsesTopPlane. F3DFloor::top is the CONTROL sector's ceiling plane,
		// so the surface you stand on is `top` -- this was implemented the other way round, with a
		// comment explaining the inversion backwards.
		const F3DFloor::planeref &pr = ComputeDecalUsesTopPlane(ceiling) ? rover->top : rover->bottom;
		if (pr.plane != NULL) return pr.plane;
	}
	return ceiling ? &sec->ceilingplane : &sec->floorplane;
}

static FlatDecal g_decals[kMaxFlatDecals];
// [rc4l] Split by cause: "the floor branch never ran" and "it ran and the template was empty" look
// identical from in front of an unmarked floor.
int g_impactWall = 0, g_impactFloor = 0, g_impactCeiling = 0, g_impactOther = 0,
    g_impactSuppressed = 0;
void NoteImpact(int hitType, bool noImpactFlag, bool noDecalFlag)
{
	if (noImpactFlag || noDecalFlag) { g_impactSuppressed++; return; }
	// [rc4l] The real ETraceResult order: None, Floor, Ceiling, Wall, Actor. Guessed at once as
	// Wall-first, which made a working feature report "6 wall hits, 0 floor hits" while it was
	// spawning six floor decals -- a diagnostic that lies is worse than none.
	if (hitType == TRACE_HitFloor) g_impactFloor++;
	else if (hitType == TRACE_HitCeiling) g_impactCeiling++;
	else if (hitType == TRACE_HitWall) g_impactWall++;
	else g_impactOther++;
}
float g_lastHitZ=0, g_lastPlaneSpawn=0, g_lastPlaneNow=0;
bool g_lastRover=false;
float g_lastX=0, g_lastY=0, g_lastZ=0, g_lastHW=0, g_lastHH=0, g_lastAlpha=0;
bool g_lastRed=false;
unsigned int g_lastShade=0;
int g_lastLight=0;
int g_spawnTries = 0, g_spawnNoTemplate = 0, g_spawnNoTexture = 0, g_spawnNoSector = 0,
    g_spawnNoSurface = 0, g_emitted = 0;
static int g_count = 0;   // how many slots are live, up to kMaxFlatDecals
static int g_next = 0;    // where the next one goes

int FlatDecalCount() { return g_count; }

void ClearFlatDecals()
{
	g_count = 0;
	g_next = 0;
}

void SpawnFlatDecal(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z, bool ceiling,
                    F3DFloor *rover)
{
	g_spawnTries++;
	if (!fua_flat_decals) return;
	if (tpl == NULL) { g_spawnNoTemplate++; return; }
	if (!tpl->PicNum.isValid()) { g_spawnNoTexture++; return; }

	sector_t *sec = P_PointInSector(x, y);
	if (sec == NULL) { g_spawnNoSector++; return; }

	// [rc4l] Honour the surface's own refusal, exactly as the wall path does.
	//
	// ANIMDEFS marks every animated texture bNoDecals unless it says `allowdecals`, and
	// DBaseDecal::StaticCreate drops the decal when the wall texture has that flag. A decal glued to
	// a flowing nukage floor would sit still while the texture moved underneath it, which is why the
	// rule exists -- and it has to hold on floors for the same reason it holds on walls.
	{
		const sector_t *ts = sec;
		int side = ceiling ? sector_t::ceiling : sector_t::floor;
		if (rover != NULL && rover->model != NULL)
		{
			ts = rover->model;
			const F3DFloor::planeref &pr = ComputeDecalUsesTopPlane(ceiling) ? rover->top : rover->bottom;
			side = pr.isceiling ? sector_t::ceiling : sector_t::floor;
		}
		FTexture *surf = TexMan[ts->GetTexture(side)];
		if (surf == NULL || surf->bNoDecals) { g_spawnNoSurface++; return; }
	}

	FlatDecal &d = g_decals[g_next];
	d.x = FIXED2FLOAT(x);
	d.y = FIXED2FLOAT(y);
	d.z = FIXED2FLOAT(z);
	d.ceiling = ceiling;
	d.pic = tpl->PicNum;
	// ScaleX/ScaleY are fixed-point multipliers on the decal graphic's own size.
	d.scaleX = FIXED2FLOAT(tpl->ScaleX);
	d.scaleY = FIXED2FLOAT(tpl->ScaleY);
	if (d.scaleX <= 0.f) d.scaleX = 1.f;
	if (d.scaleY <= 0.f) d.scaleY = 1.f;
	// Alpha is stored as (actor->alpha >> 1), so it runs 0..32768 rather than 0..65536.
	d.alpha = tpl->Alpha / 32768.f;
	if (d.alpha <= 0.f || d.alpha > 1.f) d.alpha = 1.f;
	d.shadeColor = tpl->ShadeColor;
	d.redToAlpha = !!(tpl->RenderStyle.Flags & STYLEF_RedIsAlpha);
	d.additive = tpl->RenderStyle.BlendOp == STYLEOP_Add &&
	             tpl->RenderStyle.DestAlpha == STYLEALPHA_One;
	d.rover = rover;
	d.planeZ = 0.f;
	d.planeSampled = false;

	g_next = (g_next + 1) % kMaxFlatDecals;
	if (g_count < kMaxFlatDecals) g_count++;
}

void RegisterFlatDecals()
{
	if (!fua_flat_decals || !gl_wallmesh || g_count == 0) return;

	g_emitted = 0;
	for (int i = 0; i < g_count; i++)
	{
		const FlatDecal &d = g_decals[i];
		FMaterial *mat = FMaterial::ValidateTexture(d.pic, true, true);
		if (mat == NULL) continue;

		// Half-extents in map units. A decal graphic is authored at 1 unit per texel like a sprite.
		const float hw = mat->TextureWidth() * d.scaleX * 0.5f;
		const float hh = mat->TextureHeight() * d.scaleY * 0.5f;
		if (hw <= 0.f || hh <= 0.f) continue;

		// [rc4l] Draw at the height it was SHOT, moved by however far its sector's plane has since
		// travelled.
		//
		// Reading the height straight off the sector plane instead was wrong in the common case: a
		// shot that lands on a 3D floor reports a hit height of 192 while the sector's own floor is
		// still at 0, so the decal was drawn 192 units underneath the surface it marked and was
		// invisible. Tracking the DELTA keeps the decal where the bullet hit and still lets it ride a
		// lift, which is the only reason to consult the plane at all.
		// [rc4l] Look the sector up the SAME way it was looked up at spawn.
		//
		// This used to store `sec - sectors` and index the array back. Measured on dbab02: the plane
		// read 128 when the shot landed and 0 when the frame drew -- the same call, two answers, so
		// the sector coming back was not the sector that went in. P_PointInSector is what
		// SpawnFlatDecal uses, so using it here too makes the two agree by construction rather than
		// by an index that has to survive a round trip.
		sector_t *sec = P_PointInSector(FLOAT2FIXED(d.x), FLOAT2FIXED(d.y));
		if (sec == NULL) continue;
		const secplane_t *plane = DecalPlane(sec, d.rover, d.ceiling);
		const float planeNow = FIXED2FLOAT(plane->ZatPoint(FLOAT2FIXED(d.x), FLOAT2FIXED(d.y)));
		// First frame this decal is drawn: that is when its surface height becomes comparable with
		// every later frame's.
		FlatDecal &mut = g_decals[i];
		if (!mut.planeSampled) { mut.planeZ = planeNow; mut.planeSampled = true; }
		// The depth-bias pipeline handles the coplanar fight; the offset only keeps the quad on the
		// correct SIDE of the plane, so a decal is never swallowed by the surface it marks.
		// [rc4l] One unit of tolerance: a plane that was within a unit of the hit when the shot landed is
		// the surface it landed on; anything further away is a different surface and must not drag the
		// decal with it.
		const float pz = ComputeDecalHeight(d.z, mut.planeZ, planeNow, d.ceiling, 0.05f, 1.0f);

		FFlatVertex quad[4];
		// Wound so the decal faces the side it was shot from: a floor decal is seen from above and a
		// ceiling decal from below, exactly the distinction the flat mesh makes for its own planes.
		if (!d.ceiling)
		{
			quad[0].Set(d.x - hw, pz, d.y - hh, 0.f, 0.f);
			quad[1].Set(d.x + hw, pz, d.y - hh, 1.f, 0.f);
			quad[2].Set(d.x + hw, pz, d.y + hh, 1.f, 1.f);
			quad[3].Set(d.x - hw, pz, d.y + hh, 0.f, 1.f);
		}
		else
		{
			quad[0].Set(d.x - hw, pz, d.y + hh, 0.f, 1.f);
			quad[1].Set(d.x + hw, pz, d.y + hh, 1.f, 1.f);
			quad[2].Set(d.x + hw, pz, d.y - hh, 1.f, 0.f);
			quad[3].Set(d.x - hw, pz, d.y - hh, 0.f, 0.f);
		}

		// Lit by the sector it sits in, like the plane under it.
		FColormap cm;
		cm.Clear();
		cm.LightColor = sec->ColorMap->Color;
		cm.FadeColor = sec->ColorMap->Fade;
		cm.desaturation = sec->ColorMap->Desaturate;
		const int light = d.ceiling ? sec->GetCeilingLight() : sec->GetFloorLight();

		RegisterDecal(quad, mat, 0, false, d.additive, d.alpha,
			light, getExtraLight(), cm,
			d.redToAlpha, d.redToAlpha ? (unsigned int)d.shadeColor : 0xffffffu,
			d.x, d.y, pz);
		// [rc4l] The first decal's actual numbers, so "it is not being drawn" can be told apart from
		// "it is being drawn somewhere I am not looking, or at zero size, or in black on black".
		// The LAST one emitted, not the first: the ring holds older decals ahead of the newest, and
		// reporting the oldest made every check look like nothing had spawned.
		{
			g_lastX = d.x; g_lastY = d.y; g_lastZ = pz;
			g_lastHitZ = d.z; g_lastPlaneSpawn = mut.planeZ; g_lastPlaneNow = planeNow;
			g_lastRover = (d.rover != NULL);
			g_lastHW = hw; g_lastHH = hh;
			g_lastAlpha = d.alpha; g_lastRed = d.redToAlpha;
			g_lastShade = d.shadeColor; g_lastLight = light;
		}
		g_emitted++;
	}
}

}} // namespace zx::levelmesh

//==========================================================================
//
// fua_flatdecals
//
// [rc4l] Are floor decals being SPAWNED, and are they being EMITTED?
//
// Those are different failures with the same symptom -- an unmarked floor -- and no screenshot can
// tell them apart. The spawn counter answers whether P_LineAttack's floor branch is reached at all;
// the emit counter answers whether the records survive as far as the mesh.
//
//==========================================================================

CCMD( fua_flatdecals )
{
	Printf( "flat decals: %d live, %d spawn attempts (%d refused: no template %d, no texture %d, "
			"no sector %d), %d emitted last frame\n",
			zx::levelmesh::FlatDecalCount( ), zx::levelmesh::g_spawnTries,
			zx::levelmesh::g_spawnNoTemplate + zx::levelmesh::g_spawnNoTexture +
				zx::levelmesh::g_spawnNoSector,
			zx::levelmesh::g_spawnNoTemplate, zx::levelmesh::g_spawnNoTexture,
			zx::levelmesh::g_spawnNoSector + zx::levelmesh::g_spawnNoSurface, zx::levelmesh::g_emitted );
	Printf( "  impacts seen: wall %d, floor %d, ceiling %d, other %d, suppressed %d\n",
			zx::levelmesh::g_impactWall, zx::levelmesh::g_impactFloor,
			zx::levelmesh::g_impactCeiling, zx::levelmesh::g_impactOther,
			zx::levelmesh::g_impactSuppressed );
	Printf( "  first emitted: at (%.0f, %.0f, %.1f) half-size %.1f x %.1f, alpha %.2f, "
			"redToAlpha %d, shade %06x, light %d\n",
			zx::levelmesh::g_lastX, zx::levelmesh::g_lastY, zx::levelmesh::g_lastZ,
			zx::levelmesh::g_lastHW, zx::levelmesh::g_lastHH, zx::levelmesh::g_lastAlpha,
			zx::levelmesh::g_lastRed ? 1 : 0, zx::levelmesh::g_lastShade & 0xffffff,
			zx::levelmesh::g_lastLight );
	Printf( "  placement: hit z %.1f, plane %.1f -> %.1f (delta %.1f), drawn at %.1f, on a 3D floor %d\n",
			zx::levelmesh::g_lastHitZ, zx::levelmesh::g_lastPlaneSpawn, zx::levelmesh::g_lastPlaneNow,
			zx::levelmesh::g_lastPlaneNow - zx::levelmesh::g_lastPlaneSpawn,
			zx::levelmesh::g_lastZ, zx::levelmesh::g_lastRover ? 1 : 0 );
}
