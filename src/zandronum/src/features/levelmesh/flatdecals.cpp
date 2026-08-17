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
#include "g_level.h"   // level.maptime, for decal fade
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
	// [rc4l] When it was made, and whether it is meant to be permanent.
	//
	// A decal template can carry an FDecalAnimator -- a fader, a stretcher -- and DImpactDecal runs
	// it so the mark changes and usually disappears. Ignoring it left a plasma scorch, which is
	// ADDITIVE, sitting at full brightness on the floor forever: a glowing white blob that never
	// went away. A decal with no animator is permanent, exactly as a bullet hole should be.
	int        spawnTic;
	bool       fades;
	// [rc4l] How far off the surface this one sits.
	//
	// A template's lower decal and the decal above it land at the same point with the same height,
	// and two coplanar quads at an identical distance have no settled order -- the sort swapped them
	// frame to frame and the scorch flickered through the glow. Giving the lower one less clearance
	// separates them once and for all, which is also what "lower" means.
	float      clearance;
};

// How long an animated decal takes to fade out. The engine's own faders vary; this is one length for
// all of them, which is wrong in detail and much closer than "never".
static const int kFadeTics = 105;   // three seconds at 35 tics

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
// This frame's decals expressed as volumes, for the projected pass. Rebuilt every frame from the
// same records, so nothing about spawning or fading changes.
static TArray<ProjectedDecal> g_projected;
int GetProjectedDecals(const ProjectedDecal **out)
{
	*out = g_projected.Size() ? &g_projected[0] : NULL;
	return (int)g_projected.Size();
}
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
int g_missileTries = 0, g_missileNoGen = 0, g_missileNotPlane = 0;
void NoteMissileImpact(bool hasGenerator, bool onFloor, bool onCeiling)
{
	g_missileTries++;
	if (!hasGenerator) { g_missileNoGen++; return; }
	if (!onFloor && !onCeiling) g_missileNotPlane++;
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
                    F3DFloor *rover, DWORD shadeOverride, bool isLower)
{
	g_spawnTries++;
	if (!fua_flat_decals) return;
	if (tpl == NULL) { g_spawnNoTemplate++; return; }
	if (!tpl->PicNum.isValid()) { g_spawnNoTexture++; return; }

	// [rc4l] Place the LOWER decal first, exactly as DImpactDecal::StaticCreate does.
	//
	// A decal template can name another to sit underneath it, and that is how a plasma mark works:
	// the bright scorch on top carries an animator and fades, while the dark burn beneath has none
	// and stays. Skipping it meant the glow faded away and left clean floor -- the mark that is
	// supposed to survive was never placed at all.
	//
	// The colour is only inherited when the two templates agree on their own, matching the engine:
	// a custom colour meant for the top decal has no business tinting the burn under it.
	if (tpl->LowerDecal != NULL)
	{
		const FDecalTemplate *low = tpl->LowerDecal->GetDecal();
		if (low != NULL && low != tpl)
			SpawnFlatDecal(low, x, y, z, ceiling, rover,
				(tpl->ShadeColor == low->ShadeColor) ? shadeOverride : 0, true);
	}

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
	d.shadeColor = (shadeOverride != 0) ? shadeOverride : tpl->ShadeColor;
	d.redToAlpha = !!(tpl->RenderStyle.Flags & STYLEF_RedIsAlpha);
	d.additive = tpl->RenderStyle.BlendOp == STYLEOP_Add &&
	             tpl->RenderStyle.DestAlpha == STYLEALPHA_One;
	d.rover = rover;
	d.planeZ = 0.f;
	d.planeSampled = false;
	d.spawnTic = level.maptime;
	d.clearance = isLower ? 0.02f : 0.06f;
	d.fades = (tpl->Animator != NULL);

	g_next = (g_next + 1) % kMaxFlatDecals;
	if (g_count < kMaxFlatDecals) g_count++;
}

void RegisterFlatDecals()
{
	g_projected.Clear();
	if (!fua_flat_decals || g_count == 0) return;

	g_emitted = 0;
	for (int i = 0; i < g_count; i++)
	{
		const FlatDecal &d = g_decals[i];
		FMaterial *mat = FMaterial::ValidateTexture(d.pic, true, true);
		if (mat == NULL) continue;

		// Animated decals fade and go. Everything else stays.
		float fade = 1.f;
		if (d.fades)
		{
			const int age = level.maptime - d.spawnTic;
			if (age >= kFadeTics) continue;
			if (age > 0) fade = 1.f - (float)age / (float)kFadeTics;
		}

		const float hw = mat->TextureWidth() * d.scaleX * 0.5f;
		const float hh = mat->TextureHeight() * d.scaleY * 0.5f;
		if (hw <= 0.f || hh <= 0.f) continue;

		sector_t *sec = P_PointInSector(FLOAT2FIXED(d.x), FLOAT2FIXED(d.y));
		if (sec == NULL) continue;

		const secplane_t *plane = DecalPlane(sec, d.rover, d.ceiling);
		const float planeNow = FIXED2FLOAT(plane->ZatPoint(FLOAT2FIXED(d.x), FLOAT2FIXED(d.y)));
		FlatDecal &mut = g_decals[i];
		if (!mut.planeSampled) { mut.planeZ = planeNow; mut.planeSampled = true; }
		// No clearance offset: a projected decal is a VOLUME centred on the surface, not a quad that
		// has to be lifted clear of it.
		const float pz = ComputeDecalHeight(d.z, mut.planeZ, planeNow, d.ceiling, 0.f, 1.0f);

		// Lit by the sector it sits in, like the plane under it.
		const int light = d.ceiling ? sec->GetCeilingLight() : sec->GetFloorLight();
		FColormap cm;
		cm.Clear();
		cm.LightColor = sec->ColorMap->Color;
		cm.FadeColor = sec->ColorMap->Fade;
		cm.desaturation = sec->ColorMap->Desaturate;

		MeshPiece lit;
		CaptureShading(light, getExtraLight(), cm, lit);

		ProjectedDecal pd;
		pd.x = d.x; pd.y = d.y; pd.z = pz;
		pd.halfW = hw; pd.halfH = hh;
		// [rc4l] How far through the surface the box reaches.
		//
		// Deep enough to survive the surface being a little away from where the decal was recorded --
		// a sloped floor, a step, a lift caught mid-move -- and shallow enough that it cannot reach
		// the floor below or the ceiling above. Eight units is about a quarter of a step.
		pd.halfDepth = 8.f;
		pd.material = mat;
		pd.r = lit.colorR; pd.g = lit.colorG; pd.b = lit.colorB;
		if (d.redToAlpha)
		{
			pd.r *= ((d.shadeColor >> 16) & 0xff) / 255.f;
			pd.g *= ((d.shadeColor >> 8) & 0xff) / 255.f;
			pd.b *= (d.shadeColor & 0xff) / 255.f;
		}
		pd.a = d.alpha * fade;
		pd.additive = d.additive;
		pd.redToAlpha = d.redToAlpha;
		pd.ceiling = d.ceiling;
		g_projected.Push(pd);

		g_lastX = d.x; g_lastY = d.y; g_lastZ = pz;
		g_lastHitZ = d.z; g_lastPlaneSpawn = mut.planeZ; g_lastPlaneNow = planeNow;
		g_lastRover = (d.rover != NULL);
		g_lastHW = hw; g_lastHH = hh;
		g_lastAlpha = pd.a; g_lastRed = d.redToAlpha;
		g_lastShade = d.shadeColor; g_lastLight = light;
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
	Printf( "  missile impacts: %d seen, %d with no decal generator, %d not on a plane\n",
			zx::levelmesh::g_missileTries, zx::levelmesh::g_missileNoGen,
			zx::levelmesh::g_missileNotPlane );
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
