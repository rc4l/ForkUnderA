// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-traces/acstraces.h"

#include <cstddef>
#include <memory>
#include <vector>

#include "actor.h"
#include "c_console.h"
#include "doomdata.h"
#include "doomstat.h"
#include "farchive.h"
#include "name.h"
#include "p_3dfloors.h"
#include "p_acs.h"
#include "p_local.h"
#include "p_tags.h"
#include "p_trace.h"
#include "r_defs.h"
#include "r_sky.h"
#include "r_state.h"
#include "tables.h"
#include "textures/textures.h"
#include "features/acs-fields/computation/fields_compute.h"
#include "features/acs-handles/acshandles.h"
#include "features/acs-queries/acsqueries.h"

// [Doruk-Aksoy] The SAVEVER from which scripts save their tracers, and from which tracers save their rules.
static const int kTracerSaveVersion = 4541;
static const int kTracerRulesSaveVersion = 4542;
static const int kTracerCallbackSaveVersion = 4543;
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

// [Doruk-Aksoy] Whether this map already warned of a script out of tracers, and of a callback that gave no action.
bool g_warnedFull = false;
bool g_warnedCallback = false;

// [Doruk-Aksoy] How many callbacks are running, during which no trace may start.
int g_callbackDepth = 0;

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
	// [Doruk-Aksoy] Trace() leaves an actor hit's sector as the last wall's, so it is the actor's own.
	const sector_t *sector = res.HitType == TRACE_HitActor && res.Actor != NULL ? res.Actor->Sector : res.Sector;
	r.sector = SectorOf(sector);
	r.sectorTag = sector != NULL ? tagManager.GetFirstSectorTag(&sectors[sector->sectornum]) : 0;
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

// [Doruk-Aksoy] Whether a hit is sky, as Trace()'s TRACE_NoSky judges it.
bool IsSky(const FTraceResults &res)
{
	if (res.HitType == TRACE_HitFloor || res.HitType == TRACE_HitCeiling)
		return res.HitTexture == skyflatnum;
	return res.HitType == TRACE_HitWall && res.Tier == TIER_Upper && res.Line != NULL && res.Line->backsector != NULL
		&& res.Line->frontsector->GetTexture(sector_t::ceiling) == skyflatnum && res.Line->backsector->GetTexture(sector_t::ceiling) == skyflatnum;
}

// [Doruk-Aksoy] One fua_Trace: a copy of the tracer's rules (a callback may change them), their actor filters looked up once, and the hits they keep.
struct RuleTrace
{
	RuleTrace(ScriptTracers &ts, int32_t i, AActor *who, const std::vector<Rule> &r, const Fixed3 &d, bool sky)
		: tracers(ts), id(i), activator(who), rules(r), dir(d), noSky(sky), filters(r.size())
	{
		for (size_t i = 0; i < rules.size(); ++i)
		{
			if (ComputeRuleFilterArg(rules[i].filter.kind) == acsqueries::kArgInvalid || rules[i].filter.kind >= kTqLineFlag || rules[i].filter.kind == 0)
				continue;
			filters[i].reset(new acsqueries::ActorFilters(std::vector<acsqueries::Filter>(1, rules[i].filter)));
			// [Doruk-Aksoy] A rule whose handle's actor is gone never matches.
			if (!filters[i]->Resolve())
				filters[i].reset();
		}
	}

	bool Test(const Rule &rule, const FTraceResults &res)
	{
		const size_t i = size_t(&rule - rules.data());
		switch (rule.filter.kind)
		{
		case kTqLineFlag:
		case kTqSpecial: return res.Line != NULL && ComputeLineFilterMatch(rule.filter, res.Line->flags, res.Line->special);
		case kTqTexture: return (TextureOf(res) == rule.filter.a) == (rule.filter.b != 0);
		default: return res.Actor != NULL && filters[i] != NULL && filters[i]->Matches(res.Actor);
		}
	}

	ETraceStatus Decide(const FTraceResults &res)
	{
		// [Doruk-Aksoy] A client's snapshot mirror only draws the actor scripts read, so no rule ever sees one; an actor a callback removed is passed by.
		if (res.HitType == TRACE_HitActor && res.Actor != NULL && ((res.Actor->NetworkFlags & NETFL_MIRROR) || (res.Actor->ObjectFlags & OF_EuthanizeMe)))
			return TRACE_Skip;
		decided = true;
		lastType = res.HitType;
		lastDistance = res.Distance;
		// [Doruk-Aksoy] With TRF_NOSKY the sky is no hit: the trace ends there and keeps nothing of it.
		if (noSky && IsSky(res))
			return TRACE_Stop;
		const int kind = ComputeHitKind(res.HitType, res.Tier);
		int action = ComputeRuleAction(rules, kind, [&](const Rule &r) { return Test(r, res); });
		if (action == kActCallback)
			action = Call(res);
		ComputeRuleRecord(run, action, HitOf(res, dir));
		return ETraceStatus(ComputeRuleStatus(action, kind));
	}

	// [Doruk-Aksoy] The tracer's callback's action for a hit, which it reads as the tracer's last result; one that waits or gives none stops the trace.
	int Call(const FTraceResults &res)
	{
		const Tracer *t = tracers.Find(id);
		if (t == NULL || t->callback == 0)
			return kActStop;
		const int32_t callback = t->callback, args[2] = { id, t->context };
		tracers.SetResult(id, HitOf(res, dir), run.hits);
		bool ok = false;
		++g_callbackDepth;
		const int32_t value = DLevelScript::RunFunctionNow(callback, activator, &tracers, args, 2, ok);
		--g_callbackDepth;
		if (!ok)
		{
			if (!g_warnedCallback)
				Printf(TEXTCOLOR_ORANGE "fua_TraceCallback: a callback waited, ended without returning, or its function is gone; that hit stops the trace.\n");
			g_warnedCallback = true;
			return kActStop;
		}
		return ComputeCallbackAction(value);
	}

	// [Doruk-Aksoy] Whether the rules already saw the hit the trace ended on.
	bool Saw(const FTraceResults &res) const
	{
		return decided && res.HitType == lastType && res.Distance == lastDistance;
	}

	ScriptTracers &tracers;
	const int32_t id;
	AActor *const activator;
	const std::vector<Rule> rules;
	const Fixed3 dir;
	const bool noSky;
	std::vector<std::unique_ptr<acsqueries::ActorFilters>> filters;
	RuleRun run;
	bool decided = false;
	int lastType = TRACE_HitNone;
	fixed_t lastDistance = 0;
};

ETraceStatus CheckRules(FTraceResults &res, void *userdata)
{
	return static_cast<RuleTrace *>(userdata)->Decide(res);
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
	// [Doruk-Aksoy] A new trace drops the last one's hits but keeps the rules; none starts inside a callback.
	if (g_callbackDepth > 0 || !tracers.SetResult(id, ComputeEmptyHit(), std::vector<Hit>()))
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
	::Trace(sx, sy, fixed_t::FromRaw(start.z), P_PointInSector(sx, sy), fixed_t::FromRaw(dir.x), fixed_t::FromRaw(dir.y), fixed_t::FromRaw(dir.z),
		fixed_t::FromRaw(distance), masks.actors, masks.lines, t1, res, masks.trace, CheckLineTrace, &data);
	const Hit hit = HitOf(res, dir);
	tracers.SetResult(id, hit, hit.v[kTrHitType] != kHitNone ? std::vector<Hit>(1, hit) : std::vector<Hit>());
	return hit.v[kTrHitType];
}

bool TraceRule(ScriptTracers &tracers, int32_t id, int kind, int action, int count, int filter, int32_t a, int32_t b, const char *name)
{
	if (count < 1)
		filter = 0;
	const acsqueries::ArgKind arg = ComputeRuleFilterArg(filter);
	if (arg != acsqueries::kArgNone && arg != acsqueries::kArgInvalid && count < 2)
		return false;
	Rule rule = { kind, action, { filter, arg == acsqueries::kArgNone ? 0 : a, count >= 3 ? b : ComputeRuleFilterDefaultB(filter) } };
	bool known = true;
	if (arg == acsqueries::kArgName)
	{
		if (name == NULL || *name == 0)
			return false;
		rule.filter.a = FName(name).GetIndex();
		known = filter == kTqTexture ? TexMan.CheckForTexture(name, FTexture::TEX_Any).Exists() : acsqueries::FilterNameKnown(filter, name);
	}
	else if (arg == acsqueries::kArgHandle)
		known = acshandles::ActorOf(a) != NULL;
	return tracers.AddRule(id, rule) && known;
}

bool TraceCallback(ScriptTracers &tracers, int32_t id, int32_t function, int32_t context)
{
	if (function != 0 && !DLevelScript::IsCallableFunction(function, 2))
		return false;
	return tracers.SetCallback(id, function, context);
}

int Trace(ScriptTracers &tracers, int32_t id, AActor *activator, int32_t x, int32_t y, int32_t z, int32_t dx, int32_t dy, int32_t dz, int32_t distance, int flags, int32_t ignore)
{
	if (g_callbackDepth > 0 || !tracers.SetResult(id, ComputeEmptyHit(), std::vector<Hit>()))
		return 0;
	AActor *skip = ignore != 0 ? acshandles::ActorOf(ignore) : NULL;
	Fixed3 dir;
	if ((ignore != 0 && skip == NULL) || distance <= 0 || !ComputeTraceUnit(dx, dy, dz, dir))
		return 0;
	// [Doruk-Aksoy] TRF_BLOCKSELF needs an actor to be blocked as, so it counts only with an ignored one; the rules stand in for the THRU flags.
	if (skip == NULL)
		flags &= ~kTrfBlockSelf;
	const Shooter shooter = skip == NULL ? Shooter{ false, false, false, false } : Shooter{
		(skip->flags & MF_MISSILE) != 0 || (skip->BounceFlags & BOUNCE_MBF) != 0,
		(skip->flags3 & MF3_NOBLOCKMONST) != 0 || ((i_compatflags & COMPATF_NOBLOCKFRIENDS) && (skip->flags & MF_FRIENDLY)),
		skip->player != NULL,
		(skip->flags & MF_FLOAT) != 0,
	};
	TraceMasks masks = ComputeLineTraceMasks(flags, shooter);
	if (flags & kTrfFuaAllLines)
		masks.lines = kMlEveryLine;
	const Tracer *tracer = tracers.Find(id);
	RuleTrace run(tracers, id, activator, tracer->rules, dir, (flags & kTrfNoSky) != 0);
	FTraceResults res;
	const fixed_t sx = fixed_t::FromRaw(x), sy = fixed_t::FromRaw(y);
	::Trace(sx, sy, fixed_t::FromRaw(z), P_PointInSector(sx, sy), fixed_t::FromRaw(dir.x), fixed_t::FromRaw(dir.y), fixed_t::FromRaw(dir.z),
		fixed_t::FromRaw(distance), masks.actors, masks.lines, skip, res, masks.trace, CheckRules, &run);
	// [Doruk-Aksoy] Trace() ends on a floor or ceiling without asking, so the rules decide that hit here.
	if (!run.run.aborted && res.HitType != TRACE_HitNone && !run.Saw(res))
		run.Decide(res);
	const Hit last = run.run.aborted ? ComputeEmptyHit() : HitOf(res, dir);
	tracers.SetResult(id, last, run.run.hits);
	return int(run.run.hits.size());
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
	const int format = arc.IsStoring() || SaveVersion >= kTracerCallbackSaveVersion ? kTracerFormatCallback
		: SaveVersion >= kTracerRulesSaveVersion ? kTracerFormatRules : kTracerFormatHits;
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
	if (!ComputeUnflattenTracers(flat.data(), flat.size(), tracers, format))
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
	g_warnedCallback = false;
}

}} // namespace zx::acstraces
