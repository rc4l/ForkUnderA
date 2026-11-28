// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_TRACES_COMPUTE_H
#define ZX_TRACES_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "features/acs-queries/computation/queries_compute.h"

namespace zx { namespace acstraces {

// [Doruk-Aksoy] The ACS function numbers, fua_ block 24050-24061.
enum Function
{
	kTracerCreate = 24050,
	kTracerFree = 24051,
	kTracerClear = 24052,
	kTraceRule = 24053,
	kTraceCallback = 24054,
	kLineTrace = 24055,
	kTrace = 24056,
	kTraceHitCount = 24057,
	kTraceResult = 24058,
	kTraceResultStr = 24059,
	kLineInfo = 24060,
	kSectorInfo = 24061,
};

// [Doruk-Aksoy] The TRF_ flags at UZDoom's values, plus ours far above them.
enum TraceFlag
{
	kTrfAbsPosition = 1,
	kTrfAbsOffset = 2,
	kTrfThruSpecies = 4,
	kTrfThruActors = 8,
	kTrfThruBlock = 16,
	kTrfThruHitscan = 32,
	kTrfNoSky = 64,
	kTrfAllActors = 128,
	kTrfSolidActors = 256,
	kTrfBlockUse = 512,
	kTrfBlockSelf = 1024,
	kTrfFuaPhysicalSize = 1 << 20,
	kTrfFuaAllLines = 1 << 21,
	kTrfFuaNoUnlagged = 1 << 22,
};

// [Doruk-Aksoy] Whose view a rule trace's rewind takes: its ignored actor's, its activator's, or no one's.
enum RewindViewer
{
	kViewerNone,
	kViewerIgnored,
	kViewerActivator,
};

// [Doruk-Aksoy] What a trace hit, at UZDoom's TRACE_Hit values.
enum HitType
{
	kHitNone,
	kHitFloor,
	kHitCeiling,
	kHitWall,
	kHitActor,
};

// [Doruk-Aksoy] What Trace() is told to do at a hit, at its ETraceStatus values.
enum TraceStatus
{
	kTraceStop,
	kTraceContinue,
	kTraceSkip,
	kTraceAbort,
};

// [Doruk-Aksoy] What a rule matches, numbered apart from the TR_ properties and hit types so one passed for another is refused.
enum HitKind
{
	kKindAny = 64,
	kKindFloor,
	kKindCeiling,
	kKindWall,
	kKindActor,
	kKind3DFloor,
};

// [Doruk-Aksoy] What a rule does with a hit: Trace()'s four, recording it and going on, and handing it to the tracer's callback.
enum Action
{
	kActStop,
	kActContinue,
	kActSkip,
	kActAbort,
	kActRecord,
	kActCallback,
};

// [Doruk-Aksoy] What a savegame's run of tracers holds: hits only (SAVEVER 4541), rules too (4542), and the callback (4543).
enum TracerFormat
{
	kTracerFormatHits = 1,
	kTracerFormatRules,
	kTracerFormatCallback,
};

// [Doruk-Aksoy] The TQ_ filters a rule tests walls and planes with, numbered apart from the FQ_ kinds.
enum WallFilter
{
	kTqLineFlag = 48,
	kTqSpecial,
	kTqTexture,
};

// [Doruk-Aksoy] The wall part Trace() reports, at its TIER_ values.
enum Tier
{
	kTierMiddle,
	kTierUpper,
	kTierLower,
	kTierFFloor,
};

// [Doruk-Aksoy] The TR_ properties a hit holds, read with fua_TraceResult and fua_TraceResultStr.
enum Prop
{
	kTrHitType,
	kTrHitActor,
	kTrHitX,
	kTrHitY,
	kTrHitZ,
	kTrDirX,
	kTrDirY,
	kTrDirZ,
	kTrDistance,
	kTrFraction,
	kTrHitLine,
	kTrLineId,
	kTrLineSide,
	kTrLinePart,
	kTrHitSector,
	kTrSectorTag,
	kTrSectorPlane,
	kTr3DFloor,
	kTrCrossedWater,
	kTrCrossed3DWater,
	kTrHitTexture,
	kTrPropCount,
};

// [Doruk-Aksoy] The LI_ properties fua_LineInfo reads.
enum LineProp
{
	kLiFlags,
	kLiSpecial,
	kLiArg0,
	kLiArg1,
	kLiArg2,
	kLiArg3,
	kLiArg4,
	kLiId,
	kLiFrontSector,
	kLiBackSector,
	kLiActivation,
	kLiAlpha,
	kLiX1,
	kLiY1,
	kLiX2,
	kLiY2,
	kLiPropCount,
};

// [Doruk-Aksoy] The SI_ properties fua_SectorInfo reads.
enum SectorProp
{
	kSiFloor,
	kSiCeiling,
	kSiLight,
	kSiSpecial,
	kSiTag,
	kSiFlags,
	kSiDamage,
	kSiPropCount,
};

// [Doruk-Aksoy] The engine's actor, line and trace flag values the masks are built from; the glue checks each against the engine.
const uint32_t kMfSolid = 0x2;
const uint32_t kMfShootable = 0x4;
const uint32_t kMlBlocking = 0x1;
const uint32_t kMlBlockMonsters = 0x2;
const uint32_t kMlBlockPlayers = 0x4000;
const uint32_t kMlBlockEverything = 0x8000;
const uint32_t kMlBlockFloaters = 0x40000;
const uint32_t kMlBlockProjectile = 0x01000000;
const uint32_t kMlBlockUse = 0x02000000;
const uint32_t kMlBlockHitscan = 0x08000000;
const uint32_t kMlEveryLine = 0xFFFFFFFFu;
const uint32_t kTraceNoSky = 1;
const uint32_t kTraceNoPassWidth = 8;
const uint32_t kFfUpperTexture = 0x20000;
const uint32_t kFfLowerTexture = 0x40000;

const size_t kMaxTracersPerScript = 16;
const size_t kMaxHits = 64;
const size_t kMaxRules = 32;

// [Doruk-Aksoy] What TRF_BLOCKSELF asks of the shooter, as UZDoom's P_LineTrace reads it.
struct Shooter
{
	bool projectile;
	bool notBlocked;
	bool player;
	bool floater;
};

struct TraceMasks
{
	uint32_t actors;
	uint32_t lines;
	uint32_t trace;
};

// [Doruk-Aksoy] Engine fixed point, 16 fraction bits.
struct Fixed3
{
	int64_t x;
	int64_t y;
	int64_t z;
};

// [Doruk-Aksoy] One hit as ACS values, indexed by Prop; the texture is a name number.
struct Hit
{
	int32_t v[kTrPropCount];
};

// [Doruk-Aksoy] One hit as Trace() reports it, with lines, sectors and the actor already turned into indices, tags and a handle.
struct RawHit
{
	int type;
	int32_t actor;
	Fixed3 pos;
	Fixed3 dir;
	int64_t distance;
	int64_t fraction;
	int line;
	int lineId;
	int side;
	int tier;
	int sector;
	int sectorTag;
	int ffloorSector;
	int crossedWater;
	int crossed3DWater;
	int32_t texture;
};

// [Doruk-Aksoy] The actor, line and trace masks fua_LineTrace's flags ask for, as UZDoom's P_LineTrace builds them.
TraceMasks ComputeLineTraceMasks(int flags, const Shooter &shooter);

// [Doruk-Aksoy] An ACS angle (1.0 a full turn) as an index into the engine's 8192-entry sine table.
uint32_t ComputeAcsAngleToFine(int32_t angle);

// [Doruk-Aksoy] The trace's unit direction from the pitch's and angle's cosines and sines.
Fixed3 ComputeTraceDirection(int64_t cosPitch, int64_t sinPitch, int64_t cosAngle, int64_t sinAngle);

// [Doruk-Aksoy] Where fua_LineTrace starts: offsets from the actor's feet, turned by its angle unless TRF_ABSOFFSET, or a map point with TRF_ABSPOSITION.
Fixed3 ComputeLineTraceStart(int flags, const Fixed3 &actor, int64_t floorclip, int64_t cosAngle, int64_t sinAngle, int32_t offsetZ, int32_t offsetForward, int32_t offsetSide);

// [Doruk-Aksoy] UZDoom's CheckLineTrace: stop at anything but an actor the flags let the trace pass, or a snapshot mirror, which it always passes.
TraceStatus ComputeLineTraceDecision(int type, bool mirror, bool thruActors, bool thruSpecies, bool sameSpecies);

// [Doruk-Aksoy] Whether a trace rewinds the other players to what its player saw, as a hitscan does: a player's trace without TRF_FUA_NOUNLAGGED, unless the world is already rewound.
bool ComputeTraceRewinds(bool playerTrace, int flags, bool alreadyRewound);

// [Doruk-Aksoy] A rule trace has no shooter of its own, so it rewinds to its ignored actor's view when that is a player, else its activator's.
RewindViewer ComputeRewindViewer(bool ignoredIsPlayer, bool activatorIsPlayer);

// [Doruk-Aksoy] UZDoom's LinePart for a wall tier: 0 upper, 1 middle, 2 lower; a 3D floor's side counts as middle.
int ComputeLinePart(int tier);

// [Doruk-Aksoy] Which of a side's three textures (0 top, 1 middle, 2 bottom) a wall hit shows, a 3D floor's by its texture flags.
int ComputeWallTextureSlot(int tier, uint32_t ffloorFlags);

// [Doruk-Aksoy] A hit as ACS values, positions cut to ACS fixed point as GetActorX cuts them.
Hit ComputeTraceHit(const RawHit &raw);

// [Doruk-Aksoy] A hit of nothing: no line, sector or 3D floor.
Hit ComputeEmptyHit();

bool ComputeTracePropValid(int prop);

// [Doruk-Aksoy] An int property of a hit, 0 for an unknown one and for the texture, which only fua_TraceResultStr reads.
int32_t ComputeTraceProp(const Hit &hit, int prop);

// [Doruk-Aksoy] The `prop`-th of `count` values, 0 outside them.
int32_t ComputeInfoProp(const int32_t *values, int count, int prop);

// [Doruk-Aksoy] One rule: the hits it matches, what it does with them, and an optional FQ_ or TQ_ filter (kind 0 for none).
struct Rule
{
	int kind;
	int action;
	acsqueries::Filter filter;
};

// [Doruk-Aksoy] A tracer: its id, the result its last trace ended on, the hits it kept, its rules, and its callback's function reference (0 for none) and context.
struct Tracer
{
	int32_t id;
	Hit last;
	std::vector<Hit> hits;
	std::vector<Rule> rules;
	int32_t callback;
	int32_t context;
};

// [Doruk-Aksoy] The action a callback's return value names; anything else, a callback among them, stops the trace.
int ComputeCallbackAction(int32_t value);

// [Doruk-Aksoy] Which kind a hit is for rules: a 3D floor's side is its own kind, not a wall; 0 for nothing.
int ComputeHitKind(int type, int tier);

bool ComputeRuleKindValid(int kind);
bool ComputeRuleActionValid(int action);

// [Doruk-Aksoy] What a rule filter's first argument is; the FQ_ shape filters don't fit a hit, so they are invalid.
acsqueries::ArgKind ComputeRuleFilterArg(int filter);

// [Doruk-Aksoy] The second argument when a call leaves it out: 1 (keep matches) for the TQ_ filters, the FQ_ defaults otherwise.
int32_t ComputeRuleFilterDefaultB(int filter);

// [Doruk-Aksoy] Whether a filter can apply to a kind: FQ_ to actors, TQ_LINEFLAG and TQ_SPECIAL to lines, TQ_TEXTURE to all but actors.
bool ComputeRuleFilterFits(int kind, int filter);

// [Doruk-Aksoy] The first rule matching a hit of `kind` (its filter passing `test`) decides; none matching stops the trace.
template <class Test>
int ComputeRuleAction(const std::vector<Rule> &rules, int kind, Test test)
{
	for (const Rule &r : rules)
		if ((r.kind == kKindAny || r.kind == kind) && (r.filter.kind == 0 || test(r)))
			return r.action;
	return kActStop;
}

// [Doruk-Aksoy] What Trace() is told for an action on a hit of `kind`: recording goes on as a skip does, and a floor or ceiling ends the trace unless it aborts.
TraceStatus ComputeRuleStatus(int action, int kind);

// [Doruk-Aksoy] Whether TQ_LINEFLAG or TQ_SPECIAL keeps a line: all of `a`'s flag bits set, or its special `a`; `b` 0 turns either round.
bool ComputeLineFilterMatch(const acsqueries::Filter &filter, uint32_t flags, int special);

// [Doruk-Aksoy] A trace's hits as its rules keep them: stops and records, at most 64, and none once one aborts.
struct RuleRun
{
	std::vector<Hit> hits;
	bool aborted = false;
};

void ComputeRuleRecord(RuleRun &run, int action, const Hit &hit);

// [Doruk-Aksoy] A direction in ACS fixed point made unit length in engine fixed point; false for none.
bool ComputeTraceUnit(int32_t dx, int32_t dy, int32_t dz, Fixed3 &dir);

// [Doruk-Aksoy] The hit at `index`, or the last result for -1; NULL past the hits.
const Hit *ComputeTracerHit(const Tracer &tracer, int index);

// [Doruk-Aksoy] One script's tracers, at most 16, found by id.
class ScriptTracers
{
public:
	// [Doruk-Aksoy] A new tracer with `id`, false when the script already holds 16.
	bool Create(int32_t id);
	Tracer *Find(int32_t id);
	const Tracer *Find(int32_t id) const;
	bool Free(int32_t id);

	// [Doruk-Aksoy] Drops the tracer's rules, callback, hits and last result.
	bool Clear(int32_t id);

	// [Doruk-Aksoy] Sets the tracer's callback function reference (0 for none) and the context it is called with.
	bool SetCallback(int32_t id, int32_t callback, int32_t context);

	// [Doruk-Aksoy] Adds a rule, false for an unknown tracer, kind, action or filter, a filter that can't fit the kind, or a 33rd rule.
	bool AddRule(int32_t id, const Rule &rule);

	// [Doruk-Aksoy] A trace's result: what it ended on, and the hits it kept, at most 64.
	bool SetResult(int32_t id, const Hit &last, const std::vector<Hit> &hits);

	size_t Count() const { return tracers.size(); }

	std::vector<Tracer> tracers;
};

// [Doruk-Aksoy] The largest id the script holds, 0 for none.
int32_t ComputeHighestTracerId(const ScriptTracers &tracers);

// [Doruk-Aksoy] The tracers as one run of ints for a savegame in the newest format, and back from a run of `format`; decoding refuses a damaged run.
std::vector<int32_t> ComputeFlattenTracers(const ScriptTracers &tracers);
bool ComputeUnflattenTracers(const int32_t *data, size_t count, ScriptTracers &tracers, int format);

// [Doruk-Aksoy] Maps every name a tracer holds, its hits' textures and its rules' class, flag and texture names, through `map`.
template <class Map>
void ComputeRemapTracerNames(ScriptTracers &tracers, Map map)
{
	for (Tracer &t : tracers.tracers)
	{
		t.last.v[kTrHitTexture] = map(t.last.v[kTrHitTexture]);
		for (Hit &h : t.hits)
			h.v[kTrHitTexture] = map(h.v[kTrHitTexture]);
		for (Rule &r : t.rules)
			if (ComputeRuleFilterArg(r.filter.kind) == acsqueries::kArgName)
				r.filter.a = map(r.filter.a);
	}
}

}} // namespace zx::acstraces

#endif
