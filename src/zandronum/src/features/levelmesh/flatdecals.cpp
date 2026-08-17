// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/flatdecals.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/computation/flatdecal_compute.h"
#include "features/levelmesh/computation/decalvolume_compute.h"

#include "r_defs.h"
#include "r_state.h"
#include "decallib.h"
#include "a_sharedglobal.h"   // DBaseDecal::RealZOnWall
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

namespace zx { namespace hwrender { void GetDecalPassStats(int &boxes, int &draws); const char *GetDecalPassBail(); }}
// Counted in a_decals.cpp, where the engine decides whether a shot gets a mark at all -- which is
// upstream of anything this module or the backend does with one.
namespace zx { namespace decalstats {
extern int g_wallTries, g_wallNoTemplate, g_wallSuppressed, g_wallNoSurface, g_wallMade;
}}

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

// [rc4l] The engine's own fade curve, not an approximation of it.
//
// This was a single 105-tic linear ramp applied to every animated decal. Every fader in the game
// disagrees with it: GoAway2, which the BFG glow and the plasma flare use, holds FULL alpha for a
// second and only then fades over three. The approximation therefore started dimming a glow the
// instant it appeared -- two thirds brightness by the time anyone saw it -- and removed it while the
// engine still had a second of it left to draw. Beside GL that reads as "the glow is much dimmer in
// Vulkan", which is how it was reported.
//
// A decal whose animator is not a fader is not faded at all: stretchers, sliders and colour changers
// leave the alpha alone and never remove the mark, so ramping them out made permanent marks vanish.
struct DecalFade
{
	int  startTic;      // when the decal was made
	int  decayStart;    // tics of full alpha after that
	int  decayTime;     // tics from there to nothing; 0 means it never fades
};

static float DecalFadeAmount(const DecalFade &f, int now)
{
	if (f.decayTime <= 0) return 1.f;
	const int age = now - f.startTic;
	if (age < f.decayStart) return 1.f;
	const int into = age - f.decayStart;
	if (into >= f.decayTime) return 0.f;
	return 1.f - (float)into / (float)f.decayTime;
}

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
	// [rc4l] When it was made, and how it is meant to disappear -- see DecalFade.
	//
	// A decal template can carry an FDecalAnimator, and DImpactDecal runs it so the mark changes and
	// usually goes. Ignoring it left a plasma scorch, which is ADDITIVE, at full brightness on the
	// floor forever: a glowing white blob that never went away.
	DecalFade  fade;
	// Marked fullbright in DECALDEF: shaded at 255 rather than at the sector's light. See
	// CaptureDecalShading -- without it a glow disappears into a dark room.
	bool       fullbright;
	// [rc4l] How far off the surface this one sits.
	//
	// A template's lower decal and the decal above it land at the same point with the same height,
	// and two coplanar quads at an identical distance have no settled order -- the sort swapped them
	// frame to frame and the scorch flickered through the glow. Giving the lower one less clearance
	// separates them once and for all, which is also what "lower" means.
	float      clearance;
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

// [rc4l] A wall decal, kept as a box with a fixed orientation.
//
// Unlike a flat decal there is nothing to re-sample per frame: a wall does not rise and fall the way
// a lift's floor does, so the basis worked out at spawn stays true. The ring is separate from the
// flat one only because the records genuinely differ -- this one has no plane to ride and no rover.
struct WallDecal
{
	float      x, y;                 // centre, map space; the height is re-asked every frame
	// [rc4l] What the height is measured FROM, rather than the height.
	//
	// A decal's stored Z is relative to a plane whenever RF_RELMASK is set, and that plane moves: a
	// door track, the front of a lift. Resolving it once at spawn left the mark hanging in the air
	// where the door used to be, while GL -- which re-resolves every time it draws -- carried its
	// copy up with the door. So the wall, the raw Z and the flags are kept, and the question is asked
	// again each frame through the same function the engine uses.
	const side_t *wall;
	fixed_t    rawZ;
	DWORD      renderFlags;
	float      upOff;                // anchor-to-centre, added after the height is resolved
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
	DecalFade  fade;
	// [rc4l] The colour this decal was last handed to the backend with, kept only so the dump can
	// print it. Resolving it on demand instead means calling gl_SetColor from a console command,
	// which mutates the render state outside a frame -- that took the engine down.
	float      lastR, lastG, lastB;
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
	float along[2], outward[2];
	if (!ComputeWallDecalAxes(FIXED2FLOAT(ld->dx), FIXED2FLOAT(ld->dy),
	                          ld->sidedef[1] == wall, along, outward)) return;
	const float ux = along[0], uy = along[1];
	const float nx = outward[0], ny = outward[1];

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
	const float alongOff = ComputeDecalAlongOffset(halfW, leftOff, flipX);
	const float upOff = ComputeDecalUpOffset(halfH, topOff, flipY);

	WallDecal &w = g_wall[g_wallNext];
	w.x = FIXED2FLOAT(dxpos) + ux * alongOff;
	w.y = FIXED2FLOAT(dypos) + uy * alongOff;
	w.wall = wall;
	w.rawZ = decal->Z;
	w.renderFlags = decal->RenderFlags;
	w.upOff = upOff;
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

	w.fade.startTic = level.maptime;
	w.fade.decayStart = w.fade.decayTime = 0;
	// An animated decal is a temporary one -- see the flat ring. Without this the plasma glow, which
	// is additive, would sit at full brightness on the wall forever.
	if (tpl != NULL) GetDecalFadeTiming(tpl->Animator, w.fade.decayStart, w.fade.decayTime);

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
	d.fade.startTic = level.maptime;
	d.fade.decayStart = d.fade.decayTime = 0;
	d.clearance = isLower ? 0.02f : 0.06f;
	GetDecalFadeTiming(tpl->Animator, d.fade.decayStart, d.fade.decayTime);
	d.fullbright = !!(tpl->RenderFlags & RF_FULLBRIGHT);

	g_next = (g_next + 1) % kMaxFlatDecals;
	if (g_count < kMaxFlatDecals) g_count++;
}

// [rc4l] Store the picture's orientation, pre-divided by its own half-extents.
//
// The division itself is ComputeDecalBasis, which is where the tests can reach it; this only unpacks
// the result into the record the backend reads. Pre-dividing is what makes the shader's mapping a
// bare dot product per axis, with nothing left to scale.
//
// The extents themselves are not kept. They used to size a box built on the CPU; the box is now an
// axis-aligned cube of the blast radius, expanded on the GPU from an index, and the only thing the
// axes are still for is turning the picture the right way up on each surface it lands on.
static bool SetDecalBasis(ProjectedDecal &pd,
                          float ux, float uy, float uz,
                          float vx, float vy, float vz,
                          float nx, float ny, float nz,
                          float halfW, float halfH, float halfDepth)
{
	const float au[3] = { ux, uy, uz }, av[3] = { vx, vy, vz }, an[3] = { nx, ny, nz };
	DecalFrame f;
	if (!ComputeDecalBasis(au, av, an, halfW, halfH, halfDepth, f)) return false;
	pd.ux = f.u[0]; pd.uy = f.u[1]; pd.uz = f.u[2];
	pd.vx = f.v[0]; pd.vy = f.v[1]; pd.vz = f.v[2];
	pd.nx = f.n[0]; pd.ny = f.n[1]; pd.nz = f.n[2];
	return true;
}


// [rc4l] A FULLBRIGHT decal takes its colour from nothing and its fog from the sector it is in.
//
// DECALDEF marks the glows fullbright -- BFGLITE, the plasma flare -- and gl_decal.cpp honours it by
// shading at light 255 with no relative light, while still fogging at the sector's own level. Both
// halves matter: shading a glow at sector light makes it vanish into a dark corridor, which is what
// a BFG mark looked like beside GL's, and fogging it at 255 would leave it bright through smoke that
// dims everything around it.
//
// CaptureShading takes ONE light level for both, so getting GL's answer means asking it twice and
// keeping the colour from the bright call.
static void CaptureDecalShading(bool fullbright, int light, const FColormap &cm, MeshPiece &out)
{
	CaptureShading(light, getExtraLight(), cm, out);
	if (!fullbright) return;
	MeshPiece bright;
	CaptureShading(255, 0, cm, bright);
	out.colorR = bright.colorR;
	out.colorG = bright.colorG;
	out.colorB = bright.colorB;
}

static void RegisterWallDecals()
{
	for (int i = 0; i < g_wallCount; i++)
	{
		const WallDecal &w = g_wall[i];
		FMaterial *mat = FMaterial::ValidateTexture(w.pic, true, true);
		if (mat == NULL) continue;

		const float fade = DecalFadeAmount(w.fade, level.maptime);
		if (fade <= 0.f) continue;

		MeshPiece lit;
		CaptureDecalShading(!!(w.renderFlags & RF_FULLBRIGHT), w.light, w.cm, lit);

		// Asked again, not remembered: see WallDecal::wall. A door moving takes its decals with it.
		if (w.wall == NULL) continue;
		const float z = FIXED2FLOAT(DBaseDecal::RealZOnWall(w.wall, w.rawZ, w.renderFlags)) + w.upOff;

		ProjectedDecal pd;
		pd.x = w.x; pd.y = w.y; pd.z = z;
		// U runs along the wall, N out of its face, and V DOWN it -- because a texture's v does.
		//
		// Doom's v runs from the TOP of a graphic downwards: gl_decal.cpp gives the quad's top vertices
		// GetVT and its bottom ones GetVB. Running V upwards instead samples every wall decal upside
		// down, which on a roughly symmetric scorch is nearly invisible in shape -- and still MOVES the
		// mark, because a graphic whose top offset is not its half-height has its content off-centre
		// and mirroring shifts the visible blob by twice that. Measured on a BFG mark as GL at y 0.537
		// of the frame and the backend at 0.417, about seventeen units up the wall.
		if (!SetDecalBasis(pd, w.ux, w.uy, 0.f,
		                  0.f, 0.f, w.flipV ? 1.f : -1.f,
		                  w.nx, w.ny, 0.f,
		                  w.halfW, w.halfH, w.halfW)) continue;
		pd.material = mat;
		pd.r = lit.colorR; pd.g = lit.colorG; pd.b = lit.colorB;
		if (w.redToAlpha)
		{
			pd.r *= ((w.shadeColor >> 16) & 0xff) / 255.f;
			pd.g *= ((w.shadeColor >> 8) & 0xff) / 255.f;
			pd.b *= (w.shadeColor & 0xff) / 255.f;
		}
		pd.a = w.alpha * fade;
		WallDecal &mutw = g_wall[i];
		mutw.lastR = pd.r; mutw.lastG = pd.g; mutw.lastB = pd.b;
		pd.additive = w.additive;
		pd.redToAlpha = w.redToAlpha;
		pd.radius = ComputeDecalReach(w.halfW, w.halfH);

		// [rc4l] Nothing else to place. The shader measures every surface inside the blast radius
		// from this one point, so a corner is not a special case and there is no second box to put
		// anywhere. What used to live here -- four candidate planes, a join value expressed in two
		// frames, a slice of the picture handed from one box to the next -- was all machinery for
		// making a plane projection behave like a point one.
		g_projected.Push(pd);
	}
}

void ForgetDecals()
{
	g_wallNext = g_wallCount = 0;
	g_next = g_count = 0;
	g_projected.Clear();
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
		const float fade = DecalFadeAmount(d.fade, level.maptime);
		if (fade <= 0.f) continue;

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
		CaptureDecalShading(d.fullbright, light, cm, lit);

		// A flat's basis is the world's: U east, V north, N up. Ceilings read V the other way, so a
		// mark seen from below is not the mirror of the same mark seen from above.
		ProjectedDecal pd;
		pd.x = d.x; pd.y = d.y; pd.z = pz;
		if (!SetDecalBasis(pd, 1.f, 0.f, 0.f,
		                  0.f, d.ceiling ? -1.f : 1.f, 0.f,
		                  0.f, 0.f, 1.f,
		                  hw, hh, hw)) continue;
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
		pd.radius = ComputeDecalReach(hw, hh);
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

// [rc4l] The wall decals, as numbers.
//
// A template can name a LOWER decal -- the black scorch under a plasma or BFG glow -- and the pair is
// meant to sit concentric. When they visibly do not, the question is which of the four inputs
// separated them: the resolved height, the anchor-to-centre offset the graphic's own top offset
// implies, the half-height, or the box depth that decides how far each carries round a corner. All
// four are here, and none of them can be read off a screenshot.
void DumpWallDecals()
{
	Printf("wall decals: %d live\n", g_wallCount);
	const int show = (g_wallCount < 8) ? g_wallCount : 8;
	for (int i = g_wallCount - show; i < g_wallCount; i++)
	{
		const WallDecal &w = g_wall[i];
		const float base = (w.wall != NULL)
			? FIXED2FLOAT(DBaseDecal::RealZOnWall(w.wall, w.rawZ, w.renderFlags)) : 0.f;
		FMaterial *mat = FMaterial::ValidateTexture(w.pic, true, true);
		// [rc4l] The RESOLVED colour, not only the inputs that went into it.
		//
		// "the glow is too dim" cannot be told apart from "the shade is wrong", "the sector light is
		// wrong" or "fullbright was not honoured" by looking at a screenshot, and each of those was
		// guessed at in turn. This is the number the shader actually receives.
		const bool fb = !!(w.renderFlags & RF_FULLBRIGHT);
		const float cr = w.lastR, cg = w.lastG, cb = w.lastB;
		Printf("  %2d  at (%.1f, %.1f)  base z %.2f + upOff %.2f = %.2f\n"
		       "      half %.1f x %.1f  radius %.1f  alpha %.2f  %s%s%s  tex %s\n"
		       "      light %d -> rgb %.2f,%.2f,%.2f  (shade %06x)\n",
			i, w.x, w.y, base, w.upOff, base + w.upOff,
			w.halfW, w.halfH, ComputeDecalReach(w.halfW, w.halfH), w.alpha,
			w.additive ? "additive" : "blended", w.redToAlpha ? " red-as-alpha" : "",
			fb ? " FULLBRIGHT" : "",
			(mat && mat->tex && mat->tex->Name.Len()) ? mat->tex->Name.GetChars() : "(none)",
			w.light, cr, cg, cb, w.shadeColor & 0xffffff,
			DecalFadeAmount(w.fade, level.maptime));
	}
	{
		int boxes = 0, draws = 0;
		zx::hwrender::GetDecalPassStats(boxes, draws);
		// [rc4l] Both counts, because "nothing is on screen" has two halves. The registered count is
		// what this module handed the backend this frame; the drawn count is what the backend kept.
		// A gap between them is the backend's, and everything missing from the registered count went
		// before that -- an expired ring entry, a texture that would not resolve, a wall gone NULL.
		// [rc4l] Spawned vs drawn, because an unmarked wall has two completely different causes and
		// they need opposite investigations. `no surface` is the engine declining: StickToWall found
		// no texture for the mark to live on, which on a two-sided line means the hit landed in the
		// open gap between its upper and lower sections. GL does the same -- that mark never existed,
		// so looking for it in the backend is looking in the wrong place.
		Printf("  spawn attempts: %d made, %d no surface (two-sided line's open gap), "
		       "%d no template, %d suppressed by the wall\n",
			zx::decalstats::g_wallMade, zx::decalstats::g_wallNoSurface,
			zx::decalstats::g_wallNoTemplate, zx::decalstats::g_wallSuppressed);
		Printf("  registered this frame: %d\n", (int)g_projected.Size());
		Printf("  drawn last frame: %d boxes in %d draw calls\n", boxes, draws);
	}
}

}} // namespace zx::levelmesh

CCMD( fua_walldecals )
{
	zx::levelmesh::DumpWallDecals( );
}

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
