// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-traces/acstraces.h"

#include <cstddef>
#include <vector>

#include "actor.h"
#include "c_console.h"
#include "doomdata.h"
#include "doomstat.h"
#include "farchive.h"
#include "name.h"
#include "p_3dfloors.h"
#include "p_local.h"
#include "p_tags.h"
#include "p_trace.h"
#include "r_defs.h"
#include "r_state.h"
#include "tables.h"
#include "textures/textures.h"
#include "features/acs-fields/computation/fields_compute.h"
#include "features/acs-handles/acshandles.h"
#include "features/acs-queries/acsqueries.h"

// [Doruk-Aksoy] The SAVEVER from which scripts save their tracers.
static const int kTracerSaveVersion = 4541;
extern int SaveVersion;

namespace zx { namespace acstraces {

static_assert(kMfSolid == MF_SOLID && kMfShootable == MF_SHOOTABLE, "trace actor masks match the engine's flags");
static_assert(kMlBlocking == ML_BLOCKING && kMlBlockMonsters == ML_BLOCKMONSTERS && kMlBlockPlayers == ML_BLOCK_PLAYERS
	&& kMlBlockEverything == ML_BLOCKEVERYTHING && kMlBlockFloaters == ML_BLOCK_FLOATERS && kMlBlockProjectile == ML_BLOCKPROJECTILE
	&& kMlBlockUse == ML_BLOCKUSE && kMlBlockHitscan == ML_BLOCKHITSCAN, "trace line masks match the engine's flags");
static_assert(kTraceNoSky == TRACE_NoSky && kTraceNoPassWidth == TRACE_NoPassWidth, "trace flags match Trace()'s");
static_assert(kHitNone == TRACE_HitNone && kHitFloor == TRACE_HitFloor && kHitCeiling == TRACE_HitCeiling && kHitWall == TRACE_HitWall
	&& kHitActor == TRACE_HitActor, "hit types match Trace()'s");
static_assert(kTraceStop == TRACE_Stop && kTraceContinue == TRACE_Continue && kTraceSkip == TRACE_Skip && kTraceAbort == TRACE_Abort,
	"trace decisions match Trace()'s");
static_assert(kTierMiddle == TIER_Middle && kTierUpper == TIER_Upper && kTierLower == TIER_Lower && kTierFFloor == TIER_FFloor,
	"wall tiers match Trace()'s");
static_assert(kFfUpperTexture == FF_UPPERTEXTURE && kFfLowerTexture == FF_LOWERTEXTURE, "3D floor texture flags match the engine's");

namespace {

// [Doruk-Aksoy] Whether this map already warned of a script out of tracers.
bool g_warnedFull = false;

// [Doruk-Aksoy] What UZDoom's CheckLineTrace needs at each hit.
struct LineTraceData
{
	AActor *caller;
	bool thruActors;
	bool thruSpecies;
};

ETraceStatus CheckLineTrace(FTraceResults &res, void *userdata)
{
	const LineTraceData *data = static_cast<const LineTraceData *>(userdata);
	const bool sameSpecies = data->thruSpecies && res.HitType == TRACE_HitActor && res.Actor->GetSpecies() == data->caller->GetSpecies();
	const bool mirror = res.HitType == TRACE_HitActor && (res.Actor->NetworkFlags & NETFL_MIRROR) != 0;
	return ETraceStatus(ComputeLineTraceDecision(res.HitType, mirror, data->thruActors, data->thruSpecies, sameSpecies));
}

int32_t NameOf(FTextureID tex)
{
	if (!tex.isValid())
		return 0;
	FTexture *texture = TexMan[tex];
	return texture != NULL ? FName(texture->Name).GetIndex() : 0;
}

// [Doruk-Aksoy] The texture a hit shows: a wall's by its tier, a 3D floor side's from its master line, a plane's as Trace() found it.
int32_t TextureOf(const FTraceResults &res)
{
	if (res.HitType != TRACE_HitWall)
		return NameOf(res.HitTexture);
	const line_t *line = res.Tier == TIER_FFloor && res.ffloor != NULL ? res.ffloor->master : res.Line;
	const int side = res.Tier == TIER_FFloor ? 0 : res.Side;
	const side_t *sidedef = line != NULL ? line->sidedef[side] : NULL;
	if (sidedef == NULL)
		return 0;
	return NameOf(sidedef->GetTexture(ComputeWallTextureSlot(res.Tier, res.ffloor != NULL ? uint32_t(res.ffloor->flags) : 0)));
}

int SectorOf(const sector_t *sector)
{
	return sector != NULL ? sector->sectornum : -1;
}

int ControlSectorOf(const F3DFloor *ffloor)
{
	return ffloor != NULL ? SectorOf(ffloor->model) : -1;
}

Hit HitOf(const FTraceResults &res, const Fixed3 &dir)
{
	RawHit r = {};
	r.type = res.HitType;
	r.actor = res.HitType == TRACE_HitActor && res.Actor != NULL ? acshandles::HandleFor(res.Actor) : 0;
	r.pos = Fixed3{ zx::raw(res.X), zx::raw(res.Y), zx::raw(res.Z) };
	r.dir = dir;
	r.distance = zx::raw(res.Distance);
	r.fraction = zx::raw(res.Fraction);
	r.line = res.Line != NULL ? int(res.Line - lines) : -1;
	r.lineId = res.Line != NULL ? tagManager.GetFirstLineID(res.Line) : 0;
	r.side = res.Side;
	r.tier = res.Tier;
	r.sector = SectorOf(res.Sector);
	r.sectorTag = res.Sector != NULL ? tagManager.GetFirstSectorTag(&sectors[res.Sector->sectornum]) : 0;
	r.ffloorSector = ControlSectorOf(res.ffloor);
	r.crossedWater = SectorOf(res.CrossedWater);
	r.crossed3DWater = ControlSectorOf(res.Crossed3DWater);
	r.texture = TextureOf(res);
	return ComputeTraceHit(r);
}

int32_t ToAcs(fixed_t value)
{
	return acsfields::ComputeFixedToAcs(zx::raw(value));
}

} // namespace

int32_t TracerCreate(ScriptTracers &tracers)
{
	if (tracers.Count() >= kMaxTracersPerScript)
	{
		if (!g_warnedFull)
			Printf(TEXTCOLOR_ORANGE "fua_TracerCreate: a script already holds %u tracers; free one with fua_TracerFree.\n", unsigned(kMaxTracersPerScript));
		g_warnedFull = true;
		return 0;
	}
	const int32_t id = acsqueries::NextObjectId();
	tracers.Create(id);
	return id;
}

void TracerFree(ScriptTracers &tracers, int32_t id)
{
	tracers.Free(id);
}

void TracerClear(ScriptTracers &tracers, int32_t id)
{
	tracers.Clear(id);
}

int LineTrace(ScriptTracers &tracers, int32_t id, AActor *activator, int32_t h, int32_t angle, int32_t distance, int32_t pitch,
	int flags, int32_t offsetZ, int32_t offsetForward, int32_t offsetSide)
{
	if (!tracers.Clear(id))
		return 0;
	AActor *t1 = h == 0 ? activator : acshandles::ActorOf(h);
	if (t1 == NULL || distance <= 0)
		return 0;
	const uint32_t fa = ComputeAcsAngleToFine(angle), fp = ComputeAcsAngleToFine(pitch);
	const Fixed3 dir = ComputeTraceDirection(zx::raw(finecosine[fp]), zx::raw(finesine[fp]), zx::raw(finecosine[fa]), zx::raw(finesine[fa]));
	const Shooter shooter = {
		(t1->flags & MF_MISSILE) != 0 || (t1->BounceFlags & BOUNCE_MBF) != 0,
		(t1->flags3 & MF3_NOBLOCKMONST) != 0 || ((i_compatflags & COMPATF_NOBLOCKFRIENDS) && (t1->flags & MF_FRIENDLY)),
		t1->player != NULL,
		(t1->flags & MF_FLOAT) != 0,
	};
	const TraceMasks masks = ComputeLineTraceMasks(flags, shooter);
	const Fixed3 start = ComputeLineTraceStart(flags, Fixed3{ zx::raw(t1->x), zx::raw(t1->y), zx::raw(t1->z) }, zx::raw(t1->floorclip),
		zx::raw(finecosine[fa]), zx::raw(finesine[fa]), offsetZ, offsetForward, offsetSide);
	LineTraceData data = { t1, (flags & kTrfThruActors) != 0, (flags & kTrfThruSpecies) != 0 };
	FTraceResults res;
	const fixed_t sx = fixed_t::FromRaw(start.x), sy = fixed_t::FromRaw(start.y);
	Trace(sx, sy, fixed_t::FromRaw(start.z), P_PointInSector(sx, sy), fixed_t::FromRaw(dir.x), fixed_t::FromRaw(dir.y), fixed_t::FromRaw(dir.z),
		fixed_t::FromRaw(distance), masks.actors, masks.lines, t1, res, masks.trace, CheckLineTrace, &data);
	const Hit hit = HitOf(res, dir);
	tracers.SetResult(id, hit, hit.v[kTrHitType] != kHitNone ? std::vector<Hit>(1, hit) : std::vector<Hit>());
	return hit.v[kTrHitType];
}

int TraceHitCount(const ScriptTracers &tracers, int32_t id)
{
	const Tracer *t = tracers.Find(id);
	return t != NULL ? int(t->hits.size()) : 0;
}

int32_t TraceResult(const ScriptTracers &tracers, int32_t id, int prop, int index)
{
	const Tracer *t = tracers.Find(id);
	const Hit *hit = t != NULL ? ComputeTracerHit(*t, index) : NULL;
	if (hit == NULL)
		return 0;
	const int32_t value = ComputeTraceProp(*hit, prop);
	return prop == kTrHitActor && acshandles::ActorOf(value) == NULL ? 0 : value;
}

const char *TraceResultStr(const ScriptTracers &tracers, int32_t id, int prop, int index)
{
	const Tracer *t = tracers.Find(id);
	const Hit *hit = t != NULL ? ComputeTracerHit(*t, index) : NULL;
	if (hit == NULL || prop != kTrHitTexture || hit->v[kTrHitTexture] == 0)
		return "";
	return FName(ENamedName(hit->v[kTrHitTexture])).GetChars();
}

int32_t LineInfo(int line, int prop)
{
	if (line < 0 || line >= numlines)
		return 0;
	const line_t &l = lines[line];
	const int32_t values[kLiPropCount] = {
		int32_t(l.flags), l.special, l.args[0], l.args[1], l.args[2], l.args[3], l.args[4],
		tagManager.GetFirstLineID(&l), SectorOf(l.frontsector), SectorOf(l.backsector), int32_t(l.activation), ToAcs(l.Alpha),
		ToAcs(l.v1->x), ToAcs(l.v1->y), ToAcs(l.v2->x), ToAcs(l.v2->y),
	};
	return ComputeInfoProp(values, kLiPropCount, prop);
}

int32_t SectorInfo(int sector, int prop)
{
	if (sector < 0 || sector >= numsectors)
		return 0;
	const sector_t &s = sectors[sector];
	const int32_t values[kSiPropCount] = {
		ToAcs(s.GetPlaneTexZ(sector_t::floor)), ToAcs(s.GetPlaneTexZ(sector_t::ceiling)), s.lightlevel, s.special,
		tagManager.GetFirstSectorTag(&s), int32_t(s.Flags), s.damage,
	};
	return ComputeInfoProp(values, kSiPropCount, prop);
}

void SerializeScript(FArchive &arc, ScriptTracers &tracers)
{
	if (SaveVersion < kTracerSaveVersion)
	{
		if (arc.IsLoading())
			tracers.tracers.clear();
		return;
	}
	std::vector<int32_t> names;
	std::vector<int32_t> flat;
	if (arc.IsStoring())
	{
		ScriptTracers saved = tracers;
		ComputeRemapTracerNames(saved, [&](int32_t name) { return acsqueries::ComputeInternName(names, name); });
		flat = ComputeFlattenTracers(saved);
	}
	acsqueries::SerializeNamedRun(arc, names, flat);
	if (arc.IsStoring())
		return;
	if (!ComputeUnflattenTracers(flat.data(), flat.size(), tracers))
	{
		Printf(TEXTCOLOR_ORANGE "ACS traces: a script's saved tracers are damaged, so it starts with none.\n");
		tracers.tracers.clear();
		return;
	}
	ComputeRemapTracerNames(tracers, [&](int32_t index) { return acsqueries::ComputeNameAt(names, index); });
	acsqueries::NoteObjectId(ComputeHighestTracerId(tracers));
}

void EnterLevel()
{
	g_warnedFull = false;
}

}} // namespace zx::acstraces
