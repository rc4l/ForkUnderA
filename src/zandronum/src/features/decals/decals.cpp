// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/decals/decals.h"
#include "features/decals/flatdecal.h"
#include "features/decals/computation/markrules_compute.h"

#include "actor.h"
#include "a_sharedglobal.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "d_player.h"
#include "decallib.h"
#include "dthinker.h"
#include "m_random.h"
#include "network.h"
#include "p_3dfloors.h"
#include "p_local.h"
#include "p_lnspec.h"   // [rc4l] Line_Horizon
#include "p_spec.h"
#include "p_terrain.h"
#include "p_trace.h"
#include "r_defs.h"
#include "r_sky.h"
#include "r_state.h"
#include "statnums.h"
#include "textures/textures.h"

#include <math.h>

EXTERN_CVAR (Int, cl_maxdecals)
EXTERN_CVAR (Bool, cl_missiledecals)
EXTERN_CVAR (Bool, cl_spreaddecals)

// [rc4l] The stream marks the game never drew for take their choices and flips from; its name keeps it out of the game's traced random state.
static FRandom rr_marks ("ProjDecal");

// [rc4l] Marks on floors and ceilings, which Doom itself never makes; turning it off takes the ones already made away.
CUSTOM_CVAR (Bool, fua_decal_flats, true, CVAR_ARCHIVE)
{
	if (self) return;
	TArray<DFlatDecal *> made;
	TThinkerIterator<DFlatDecal> it (STAT_AUTODECAL);
	DFlatDecal *mark;
	while ((mark = it.Next ()) != NULL) made.Push (mark);
	for (unsigned i = 0; i < made.Size (); i++) made[i]->Destroy ();
}

namespace zx { namespace decals {

namespace {

// [rc4l] What fua_decal_report has counted since the level began: each surface kind's marks by verdict.
int g_counts[3][kMarkVerdicts];

MarkSettings Settings ()
{
	const MarkSettings s = { cl_maxdecals, cl_missiledecals, fua_decal_flats, NETWORK_GetState () == NETSTATE_SERVER };
	return s;
}

// [rc4l] Where a hit landed, as every source describes it: the wall and its 3D floor, or the plane it rides, with which way the hit was going.
struct Hit
{
	int kind;
	fixed_t x, y, z;
	side_t *wall;
	F3DFloor *ffloor;
	FTextureID texture;
	int anchorSector, anchorPlane;
	fixed_t anchorOffset;
	float normal[3], travel[3];
	bool portal, translucent, scrolls;
};

Hit NewHit (int kind, fixed_t x, fixed_t y, fixed_t z, float tx, float ty, float tz)
{
	Hit h = {};
	h.kind = kind;
	h.x = x; h.y = y; h.z = z;
	h.anchorSector = -1;
	h.travel[0] = tx; h.travel[1] = ty; h.travel[2] = tz;
	return h;
}

// [rc4l] Whether a 3D floor is see-through (translucent, additive or fog), whose planes and sides take no marks, as UZDoom draws none on its sides.
bool SeeThrough (const F3DFloor *rover)
{
	return rover != NULL && (rover->flags & (FF_TRANSLUCENT | FF_ADDITIVETRANS | FF_FOG)) != 0;
}

// [rc4l] Whether a texture scroller moves this sector's floor or ceiling, the flat's counterpart of the noautodecals a scroller sets on a side.
bool PlaneScrolls (const sector_t *sector, bool ceiling)
{
	const int index = int(sector - sectors);
	TThinkerIterator<DScroller> it (STAT_SCROLLER);
	DScroller *s;
	while ((s = it.Next ()) != NULL)
	{
		if (s->IsType (ceiling ? DScroller::sc_ceiling : DScroller::sc_floor) && s->GetAffectee () == index) return true;
	}
	return false;
}

// [rc4l] The plane a flat hit at the hit's height landed on: a 3D floor's within a unit of it, else the sector's own, with its texture, its normal facing the way the surface is seen and its height below the hit.
void FindPlane (Hit &h, sector_t *sector, FTextureID texture)
{
	const bool ceiling = h.kind == kSurfaceCeiling;
	TArray<F3DFloor *> &rovers = sector->e->XFloor.ffloors;
	TArray<long long> heights;
	TArray<F3DFloor *> candidates;
	for (unsigned i = 0; i < rovers.Size (); i++)
	{
		if (!(rovers[i]->flags & FF_EXISTS)) continue;
		const F3DFloor::planeref &p = ceiling ? rovers[i]->bottom : rovers[i]->top;
		heights.Push (p.plane->ZatPoint (h.x, h.y).Raw ());
		candidates.Push (rovers[i]);
	}
	const int owner = ComputePlaneOwner (h.z.Raw (), heights.Size () ? &heights[0] : NULL, (int)heights.Size (), FRACUNIT.Raw ());
	const secplane_t *plane;
	if (owner >= 0)
	{
		const F3DFloor::planeref &p = ceiling ? candidates[owner]->bottom : candidates[owner]->top;
		h.anchorSector = int(p.model - sectors);
		h.anchorPlane = p.isceiling ? 1 : 0;
		h.texture = *p.texture;
		h.translucent = SeeThrough (candidates[owner]);
		h.scrolls = PlaneScrolls (p.model, p.isceiling != 0);
		plane = p.plane;
	}
	else
	{
		h.anchorSector = int(sector - sectors);
		h.anchorPlane = ceiling ? 1 : 0;
		h.texture = texture.isValid () ? texture : sector->GetTexture (ceiling ? sector_t::ceiling : sector_t::floor);
		h.portal = sector->portals[ceiling ? sector_t::ceiling : sector_t::floor] != NULL;
		h.scrolls = PlaneScrolls (sector, ceiling);
		plane = ceiling ? &sector->ceilingplane : &sector->floorplane;
	}
	h.anchorOffset = h.z - plane->ZatPoint (h.x, h.y);
	// [rc4l] A 3D floor's top is its model's ceiling, whose normal faces down, so the normal is turned to face the side the hit came from.
	float n[3] = { FIXED2FLOAT (plane->a), FIXED2FLOAT (plane->b), FIXED2FLOAT (plane->c) };
	const float len = sqrtf (n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
	if (len < 1e-6f) { n[0] = n[1] = 0.f; n[2] = 1.f; }
	else for (int i = 0; i < 3; i++) n[i] /= len;
	const float facing = ceiling ? -1.f : 1.f;
	if (n[2] * facing < 0.f) for (int i = 0; i < 3; i++) n[i] = -n[i];
	for (int i = 0; i < 3; i++) h.normal[i] = n[i];
}

MarkSurface SurfaceOf (const Hit &h)
{
	MarkSurface s = {};
	s.kind = h.kind;
	if (h.kind == kSurfaceWall)
	{
		const FTextureID tex = DBaseDecal::TierTexture (h.wall, h.x, h.y, h.z, h.ffloor);
		const FTexture *t = tex.isValid () ? TexMan[tex] : NULL;
		s.sideOptOut = (h.wall->Flags & WALLF_NOAUTODECALS) != 0;
		s.horizon = h.wall->linedef->special == Line_Horizon;
		s.open = !tex.isValid ();
		s.textureRefuses = !s.open && (t == NULL || t->bNoDecals);
		s.translucent = SeeThrough (h.ffloor);
		return s;
	}
	const FTexture *t = h.texture.isValid () ? TexMan[h.texture] : NULL;
	s.sky = h.texture == skyflatnum;
	s.liquid = Terrains[TerrainTypes[h.texture]].IsLiquid;
	s.portal = h.portal;
	s.translucent = h.translucent;
	s.scrolls = h.scrolls;
	s.textureRefuses = t == NULL || t->bNoDecals;
	return s;
}

// [rc4l] The generator a shooter's marks use: its weapon's, else its own, as SpawnShootDecal chose.
const FDecalBase *GeneratorOf (AActor *shooter)
{
	if (shooter->player != NULL && shooter->player->ReadyWeapon != NULL) return shooter->player->ReadyWeapon->GetDefault ()->DecalGenerator;
	return shooter->DecalGenerator;
}

} // namespace

// [rc4l] The decal classes' one friend: everything that makes a decal object lives here.
class Maker
{
public:
	// [rc4l] DImpactDecal::StaticCreate as stock ran it, its lower decal first, once the rule has let the mark through.
	static DImpactDecal *Wall (const FDecalTemplate *tpl, const Hit &h, PalEntry color)
	{
		if (tpl->LowerDecal != NULL)
		{
			const FDecalTemplate *low = tpl->LowerDecal->GetDecal ();
			if (low != NULL) Wall (low, h, tpl->ShadeColor != low->ShadeColor ? PalEntry (0) : color);
		}
		DImpactDecal::CheckMax ();
		DImpactDecal *decal = new DImpactDecal (h.z);
		decal->StickToWall (h.wall, h.x, h.y, h.ffloor);
		tpl->ApplyToDecal (decal, h.wall);
		if (color != 0) decal->SetShade (color.r, color.g, color.b);
		if (cl_spreaddecals && decal->PicNum.isValid ()) decal->Spread (tpl, h.wall, h.x, h.y, h.z, h.ffloor);
		return decal;
	}

	// [rc4l] A permanent decal, which stock sticks wherever its trace stopped and never counts against the budget.
	static DBaseDecal *Permanent (const FDecalTemplate *tpl, const Hit &h)
	{
		DBaseDecal *decal = new DBaseDecal (h.z);
		decal->StickToWall (h.wall, h.x, h.y, h.ffloor);
		tpl->ApplyToDecal (decal, h.wall);
		if (cl_spreaddecals) decal->Spread (tpl, h.wall, h.x, h.y, h.z, h.ffloor);
		return decal;
	}

	// [rc4l] A floor or ceiling mark and the lower decals under it, lowest first and at most eight deep against a malformed cycle, each its own decal of the level.
	static DBaseDecal *Flats (const FDecalTemplate *tpl, const Hit &h, bool gameStream, PalEntry color)
	{
		const FDecalTemplate *chain[8];
		int depth = 0;
		for (const FDecalTemplate *t = tpl; t != NULL && depth < 8; )
		{
			chain[depth++] = t;
			if (t->LowerDecal == NULL) break;
			const FDecalTemplate *lower = gameStream ? t->LowerDecal->GetDecal () : t->LowerDecal->GetDecalWith (rr_marks);
			if (lower == t) break;
			t = lower;
		}
		DFlatDecal *top = NULL;
		for (int i = depth - 1; i >= 0; i--)
		{
			DImpactDecal::CheckMax ();
			DFlatDecal *mark = new DFlatDecal (h.z);
			chain[i]->ApplyToDecal (mark, NULL, rr_marks);
			const PalEntry shade = (i == 0 || chain[i]->ShadeColor == tpl->ShadeColor) ? color : PalEntry (0);
			if (shade != 0) mark->SetShade (shade.r, shade.g, shade.b);
			mark->ContactX = h.x; mark->ContactY = h.y; mark->ContactZ = h.z;
			for (int k = 0; k < 3; k++) { mark->Normal[k] = h.normal[k]; mark->Travel[k] = h.travel[k]; }
			mark->AnchorSector = h.anchorSector;
			mark->AnchorPlane = h.anchorPlane;
			mark->AnchorOffset = h.anchorOffset;
			mark->Sector = &sectors[h.anchorSector];
			top = mark;
		}
		return top;
	}

	// [rc4l] The one path every mark takes: refused before its template is chosen where stock refused it then, chosen, judged on its surface, then made.
	static DBaseDecal *Mark (const Hit &h, const FDecalBase *base, const MarkRequest &request, PalEntry color)
	{
		if (base == NULL) return NULL;
		const MarkSettings settings = Settings ();
		const int verdict = ComputeMarkAllowed (SurfaceOf (h), request, settings);
		if (ComputeRefusedBeforeChoice (request, settings))
		{
			g_counts[h.kind][verdict]++;
			return NULL;
		}
		const bool gameStream = ComputeChoiceFromGameStream (request.source, h.kind);
		const FDecalTemplate *tpl = gameStream ? base->GetDecal () : base->GetDecalWith (rr_marks);
		g_counts[h.kind][verdict]++;
		if (tpl == NULL || verdict != kMarkAllowed) return NULL;
		if (h.kind != kSurfaceWall) return Flats (tpl, h, gameStream, color);
		return request.permanent ? Permanent (tpl, h) : Wall (tpl, h, color);
	}
};

namespace {

// [rc4l] A trace's stop as a hit: its wall, or its floor or ceiling and the plane there.
bool HitFromTrace (const FTraceResults &trace, float tx, float ty, float tz, Hit &h)
{
	if (trace.HitType == TRACE_HitWall)
	{
		h = NewHit (kSurfaceWall, trace.X, trace.Y, trace.Z, tx, ty, tz);
		h.wall = trace.Line->sidedef[trace.Side];
		h.ffloor = trace.ffloor;
		return h.wall != NULL;
	}
	if (trace.HitType != TRACE_HitFloor && trace.HitType != TRACE_HitCeiling) return false;
	h = NewHit (trace.HitType == TRACE_HitCeiling ? kSurfaceCeiling : kSurfaceFloor, trace.X, trace.Y, trace.Z, tx, ty, tz);
	FindPlane (h, trace.Sector, trace.HitTexture);
	return true;
}

} // namespace

void MarkShot (AActor *shooter, const FTraceResults &trace, int source)
{
	Hit h;
	const float tx = FIXED2FLOAT (trace.X - shooter->x), ty = FIXED2FLOAT (trace.Y - shooter->y);
	const float tz = FIXED2FLOAT (trace.Z - (shooter->z + (shooter->height >> 1)));
	if (!HitFromTrace (trace, tx, ty, tz, h)) return;
	const MarkRequest request = { source, false };
	Maker::Mark (h, GeneratorOf (shooter), request, 0);
}

void MarkMissile (AActor *mo, line_t *line, AActor *target, fixed_t velx, fixed_t vely, fixed_t velz)
{
	const MarkRequest request = { kSourceMissile, false };
	if (line == NULL)
	{
		// [rc4l] With no line it stopped against a plane or an actor; only a plane takes a mark, found by where the missile came to rest.
		if (target != NULL) return;
		const int kind = ComputeMissilePlaneHit (mo->z.Raw (), mo->height.Raw (), mo->floorz.Raw (), mo->ceilingz.Raw (), (4 * FRACUNIT).Raw ());
		if (kind == kSurfaceWall) return;
		const bool ceiling = kind == kSurfaceCeiling;
		Hit h = NewHit (kind, mo->x, mo->y, ceiling ? mo->ceilingz : mo->floorz, FIXED2FLOAT (velx), FIXED2FLOAT (vely), FIXED2FLOAT (velz));
		FindPlane (h, mo->Sector, ceiling ? mo->ceilingpic : mo->floorpic);
		Maker::Mark (h, mo->DecalGenerator, request, 0);
		return;
	}

	int side = P_PointOnLineSide (mo->x, mo->y, line);
	if (line->sidedef[side] == NULL) side ^= 1;
	if (line->sidedef[side] == NULL) return;

	// Find the nearest point on the line, and stick a decal there
	const SQWORD den = (SQWORD)((SQWORD)line->dx*line->dx + (SQWORD)line->dy*line->dy);
	if (den == 0) return;
	const SQWORD num = (SQWORD)((SQWORD)(mo->x-line->v1->x)*line->dx+(SQWORD)(mo->y-line->v1->y)*line->dy);
	SDWORD frac;
	if (num <= 0) frac = 0;
	else if (num >= den) frac = 1<<30;
	else frac = (SDWORD)(num / (den>>30));

	Hit h = NewHit (kSurfaceWall, line->v1->x + MulScale30 (line->dx, frac), line->v1->y + MulScale30 (line->dy, frac), mo->z,
		FIXED2FLOAT (velx), FIXED2FLOAT (vely), FIXED2FLOAT (velz));
	h.wall = line->sidedef[side];
#ifdef _3DFLOORS
	// find a 3D-floor to stick to
	if (line->sidedef[side^1] != NULL)
	{
		extsector_t::xfloor &xf = line->sidedef[side^1]->sector->e->XFloor;
		for (unsigned int i = 0; i < xf.ffloors.Size(); i++)
		{
			F3DFloor *rover = xf.ffloors[i];
			if ((rover->flags&(FF_EXISTS|FF_SOLID|FF_RENDERSIDES)) == (FF_EXISTS|FF_SOLID|FF_RENDERSIDES) &&
				h.z <= rover->top.plane->ZatPoint (h.x, h.y) && h.z >= rover->bottom.plane->ZatPoint (h.x, h.y))
			{
				h.ffloor = rover;
				break;
			}
		}
	}
#endif
	Maker::Mark (h, mo->DecalGenerator, request, 0);
}

void MarkBlood (const char *bloodType, const FTraceResults &trace, fixed_t vx, fixed_t vy, fixed_t vz, PalEntry color)
{
	Hit h;
	if (!HitFromTrace (trace, FIXED2FLOAT (vx), FIXED2FLOAT (vy), FIXED2FLOAT (vz), h)) return;
	const MarkRequest request = { kSourceBlood, false };
	Maker::Mark (h, DecalLibrary.GetDecalByName (bloodType), request, color);
}

void MarkSpray (const char *name, const FTraceResults &trace)
{
	Hit h;
	if (trace.HitType != TRACE_HitWall || !HitFromTrace (trace, 0.f, 0.f, 0.f, h)) return;
	h.ffloor = NULL;   // [rc4l] stock sprays onto no 3D floor
	const MarkRequest request = { kSourceSpray, false };
	Maker::Mark (h, DecalLibrary.GetDecalByName (name), request, 0);
}

DBaseDecal *ShootMark (const FDecalTemplate *tpl, AActor *basis, sector_t *sec, fixed_t x, fixed_t y, fixed_t z, angle_t angle, fixed_t tracedist, bool permanent, int source)
{
	if (tpl == NULL || (tpl = tpl->GetDecal ()) == NULL) return NULL;

	FTraceResults trace;
	angle >>= ANGLETOFINESHIFT;
	Trace (x, y, z, sec, finecosine[angle], finesine[angle], 0, tracedist, 0, 0, NULL, trace, TRACE_NoSky);
	if (trace.HitType != TRACE_HitWall) return NULL;

	Hit h = NewHit (kSurfaceWall, trace.X, trace.Y, trace.Z, 0.f, 0.f, 0.f);
	h.wall = trace.Line->sidedef[trace.Side];
	// [rc4l] Stock sticks a permanent decal to the trace's 3D floor and an impact one to none, which is kept.
	h.ffloor = permanent ? trace.ffloor : NULL;
	const MarkRequest request = { source, permanent };
	return Maker::Mark (h, tpl, request, 0);
}

bool WallAcceptsClone (side_t *wall, fixed_t x, fixed_t y, fixed_t z, F3DFloor *ffloor, bool permanent)
{
	Hit h = NewHit (kSurfaceWall, x, y, z, 0.f, 0.f, 0.f);
	h.wall = wall;
	h.ffloor = ffloor;
	MarkSurface s = SurfaceOf (h);
	s.horizon = false;
	if (permanent) s.sideOptOut = false;
	return ComputeSurfaceVerdict (s) == kMarkAllowed;
}

bool SurfaceRefusesMarks (FTextureID texture, bool wall, DWORD sideFlags)
{
	const FTexture *t = texture.isValid () ? TexMan[texture] : NULL;
	MarkSurface s = {};
	s.kind = wall ? kSurfaceWall : kSurfaceFloor;
	s.sideOptOut = wall && (sideFlags & WALLF_NOAUTODECALS) != 0;
	s.sky = texture == skyflatnum;
	s.liquid = !wall && Terrains[TerrainTypes[texture]].IsLiquid;
	s.textureRefuses = t == NULL || t->bNoDecals;
	return ComputeSurfaceVerdict (s) != kMarkAllowed;
}

void ResetDecalReport ()
{
	for (int k = 0; k < 3; k++) for (int v = 0; v < kMarkVerdicts; v++) g_counts[k][v] = 0;
}

}} // namespace zx::decals

// [rc4l] What became of every mark since the level began, by surface and verdict, and how many are alive; `fua_decal_report reset` starts the count again.
CCMD (fua_decal_report)
{
	if (argv.argc () > 1 && stricmp (argv[1], "reset") == 0)
	{
		zx::decals::ResetDecalReport ();
		Printf ("decal report reset\n");
		return;
	}
	static const char *const kinds[3] = { "wall", "floor", "ceiling" };
	Printf ("decal report:\n");
	for (int k = 0; k < 3; k++)
		for (int v = 0; v < zx::decals::kMarkVerdicts; v++)
			if (zx::decals::g_counts[k][v] != 0) Printf ("  %s %s: %d\n", kinds[k], zx::decals::ComputeMarkVerdictName (v), zx::decals::g_counts[k][v]);
	int walls = 0, flats = 0;
	TThinkerIterator<DImpactDecal> it (STAT_AUTODECAL);
	DImpactDecal *decal;
	while ((decal = it.Next ()) != NULL)
	{
		if (decal->IsKindOf (RUNTIME_CLASS (DFlatDecal))) flats++;
		else walls++;
	}
	Printf ("  live: %d wall decals, %d floor and ceiling marks\n", walls, flats);
}
