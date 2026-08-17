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

// [rc4l] A wall decal, kept as a box with a fixed orientation.
//
// Unlike a flat decal there is nothing to re-sample per frame: a wall does not rise and fall the way
// a lift's floor does, so the basis worked out at spawn stays true. The ring is separate from the
// flat one only because the records genuinely differ -- this one has no plane to ride and no rover.
struct WallDecal
{
	float      x, y, z;              // centre, map space
	float      ux, uy;               // along the wall, unit; V is always straight up
	float      nx, ny;               // out of the wall, unit
	bool       flipV;                // the graphic is drawn mirrored top to bottom
	FTextureID pic;
	float      halfW, halfH;
	float      alpha;
	DWORD      shadeColor;
	bool       redToAlpha;
	bool       additive;
	int        light;
	FColormap  cm;
	int        spawnTic;
	bool       fades;
};
static WallDecal g_wall[kMaxFlatDecals];
static int g_wallNext = 0, g_wallCount = 0;

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
	ClearWallDecals();
}

void ClearWallDecals()
{
	g_wallCount = 0;
	g_wallNext = 0;
}

void SpawnWallDecal(const DBaseDecal *decal, const side_t *wall, const FDecalTemplate *tpl)
{
	if (!fua_flat_decals || decal == NULL || wall == NULL) return;
	if (!decal->PicNum.isValid()) return;
	const line_t *ld = wall->linedef;
	if (ld == NULL) return;

	// Along the wall, and out of it. The 2D normal of a line is its direction turned a quarter turn;
	// which of the two quarter turns is the outward one depends on the side the decal stuck to.
	const float dx = FIXED2FLOAT(ld->dx), dy = FIXED2FLOAT(ld->dy);
	const float len = sqrtf(dx * dx + dy * dy);
	if (len <= 0.f) return;
	const float ux = dx / len, uy = dy / len;
	const bool back = (ld->sidedef[1] == wall);
	const float nx = back ? -uy :  uy;
	const float ny = back ?  ux : -ux;

	FMaterial *mat = FMaterial::ValidateTexture(decal->PicNum, true, true);
	if (mat == NULL) return;
	const float sx = FIXED2FLOAT(decal->ScaleX), sy = FIXED2FLOAT(decal->ScaleY);
	const float halfW = mat->TextureWidth() * sx * 0.5f;
	const float halfH = mat->TextureHeight() * sy * 0.5f;
	if (halfW <= 0.f || halfH <= 0.f) return;

	fixed_t dxpos, dypos;
	const_cast<DBaseDecal *>(decal)->GetXY(const_cast<side_t *>(wall), dxpos, dypos);

	// [rc4l] From the decal's ANCHOR to the centre of its box.
	//
	// GetXY and GetRealZ give the point the graphic hangs from, not the middle of it, and the two are
	// only the same when the graphic's offsets happen to sit at its middle. gl_decal.cpp works the
	// same corner out as `pixpos - leftoffset` and `zpos + topoffset - height`; this is the middle of
	// that, so a decal that is offset in its lump lands where GL puts it rather than half a graphic
	// away. The flips swap which edge the offset is measured from.
	const bool flipX = !!(decal->RenderFlags & RF_XFLIP);
	const bool flipY = !!(decal->RenderFlags & RF_YFLIP);
	const float leftOff = mat->GetLeftOffset() * sx;
	const float topOff  = mat->GetTopOffset()  * sy;
	const float alongOff = halfW - (flipX ? (halfW * 2.f - leftOff) : leftOff);
	const float upOff = (flipY ? (halfH * 2.f - topOff) : topOff) - halfH;

	WallDecal &w = g_wall[g_wallNext];
	w.x = FIXED2FLOAT(dxpos) + ux * alongOff;
	w.y = FIXED2FLOAT(dypos) + uy * alongOff;
	// GetRealZ resolves the RF_RELATIVE flags -- a decal on a door track is stored relative to the
	// plane that moves it, and its stored Z alone is meaningless.
	w.z = FIXED2FLOAT(decal->GetRealZ(wall)) + upOff;
	// A flipped graphic is drawn mirrored; turning the axis round is the same thing and costs the
	// shader nothing.
	w.ux = flipX ? -ux : ux; w.uy = flipX ? -uy : uy;
	w.flipV = flipY;
	w.nx = nx; w.ny = ny;
	w.pic = decal->PicNum;
	w.halfW = halfW; w.halfH = halfH;
	w.alpha = FIXED2FLOAT(decal->Alpha);
	if (w.alpha <= 0.f || w.alpha > 1.f) w.alpha = 1.f;
	w.shadeColor = decal->AlphaColor & 0xffffff;
	w.redToAlpha = !!(decal->RenderStyle.Flags & STYLEF_RedIsAlpha);
	w.additive = decal->RenderStyle.BlendOp == STYLEOP_Add &&
	             decal->RenderStyle.DestAlpha == STYLEALPHA_One;

	// Lit once, by the sector behind the wall face, with the sidedef's own relative light. A wall
	// decal cannot change sectors the way a thing can, so there is nothing to re-read per frame.
	const sector_t *sec = wall->sector;
	w.light = wall->GetLightLevel(false, sec ? sec->lightlevel : 255, true);
	w.cm.Clear();
	if (sec != NULL && sec->ColorMap != NULL)
	{
		w.cm.LightColor = sec->ColorMap->Color;
		w.cm.FadeColor = sec->ColorMap->Fade;
		w.cm.desaturation = sec->ColorMap->Desaturate;
	}

	w.spawnTic = level.maptime;
	// An animated decal is a temporary one -- see the flat ring. Without this the plasma glow, which
	// is additive, would sit at full brightness on the wall forever.
	w.fades = (tpl != NULL && tpl->Animator != NULL);

	g_wallNext = (g_wallNext + 1) % kMaxFlatDecals;
	if (g_wallCount < kMaxFlatDecals) g_wallCount++;
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

// [rc4l] Store the box's orientation, pre-divided by its own half-extents.
//
// Doing the division here rather than in the shader is what makes the inside test a bare dot product
// per axis: `dot(P - centre, axis)` already comes out in -1..1 across the box. The unscaled extents
// are kept alongside so the vertex buffer can still build the eight corners.
static void SetDecalBasis(ProjectedDecal &pd,
                          float ux, float uy, float uz,
                          float vx, float vy, float vz,
                          float nx, float ny, float nz,
                          float halfW, float halfH, float halfDepth)
{
	pd.ux = ux / halfW;  pd.uy = uy / halfW;  pd.uz = uz / halfW;
	pd.vx = vx / halfH;  pd.vy = vy / halfH;  pd.vz = vz / halfH;
	pd.nx = nx / halfDepth; pd.ny = ny / halfDepth; pd.nz = nz / halfDepth;
	pd.halfW = halfW; pd.halfH = halfH; pd.halfDepth = halfDepth;
}

// [rc4l] How far a wall decal's box reaches THROUGH the wall.
//
// Shallower than a flat's, because the depth axis is horizontal here and a Doom wall is often the
// only thing between two rooms; reaching too far would print the mark on the far side. Sixteen is
// still enough to carry a mark round an inside corner onto the wall it meets.
static const float kWallDecalDepth = 16.f;

static void RegisterWallDecals()
{
	for (int i = 0; i < g_wallCount; i++)
	{
		const WallDecal &w = g_wall[i];
		FMaterial *mat = FMaterial::ValidateTexture(w.pic, true, true);
		if (mat == NULL) continue;

		float fade = 1.f;
		if (w.fades)
		{
			const int age = level.maptime - w.spawnTic;
			if (age >= kFadeTics) continue;
			if (age > 0) fade = 1.f - (float)age / (float)kFadeTics;
		}

		MeshPiece lit;
		CaptureShading(w.light, getExtraLight(), const_cast<FColormap &>(w.cm), lit);

		ProjectedDecal pd;
		pd.x = w.x; pd.y = w.y; pd.z = w.z;
		// U runs along the wall, V straight up, N out of its face. That is the whole difference from a
		// flat -- and it is the only difference, which is the point of carrying a basis at all.
		SetDecalBasis(pd, w.ux, w.uy, 0.f,
		                  0.f, 0.f, w.flipV ? -1.f : 1.f,
		                  w.nx, w.ny, 0.f,
		                  w.halfW, w.halfH, kWallDecalDepth);
		pd.material = mat;
		pd.r = lit.colorR; pd.g = lit.colorG; pd.b = lit.colorB;
		if (w.redToAlpha)
		{
			pd.r *= ((w.shadeColor >> 16) & 0xff) / 255.f;
			pd.g *= ((w.shadeColor >> 8) & 0xff) / 255.f;
			pd.b *= (w.shadeColor & 0xff) / 255.f;
		}
		pd.a = w.alpha * fade;
		pd.additive = w.additive;
		pd.redToAlpha = w.redToAlpha;
		g_projected.Push(pd);
	}
}

void RegisterFlatDecals()
{
	g_projected.Clear();
	if (!fua_flat_decals) return;
	RegisterWallDecals();
	if (g_count == 0) return;

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

		// [rc4l] The last number is how far THROUGH the surface the box reaches.
		//
		// Everything outside the box is discarded, so this is what decides how much of a decal
		// survives: at eight units a mark on anything but dead-flat ground came out clipped, because
		// the surface wandered out of the box within the decal's own width. Twenty-four is about
		// three quarters of a step -- deep enough to keep a mark whole across a slope or a small
		// ledge, shallow enough that it cannot reach the floor below or the ceiling above. It is also
		// what lets a mark shot into a corner creep up the adjoining wall instead of stopping dead at
		// the join, since the wall is inside the box for its first twenty-four units.
		//
		// A flat's basis is the world's: U east, V north, N up. Ceilings read V the other way, so a
		// mark seen from below is not the mirror of the same mark seen from above.
		ProjectedDecal pd;
		pd.x = d.x; pd.y = d.y; pd.z = pz;
		SetDecalBasis(pd, 1.f, 0.f, 0.f,
		                  0.f, d.ceiling ? -1.f : 1.f, 0.f,
		                  0.f, 0.f, 1.f,
		                  hw, hh, 24.f);
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
