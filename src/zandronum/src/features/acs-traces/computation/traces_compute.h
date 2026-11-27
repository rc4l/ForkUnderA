// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_TRACES_COMPUTE_H
#define ZX_TRACES_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zx { namespace acstraces {

// [Doruk-Aksoy] The ACS function numbers, fua_ block 24050-24061; 24053, 24054 and 24056 come with the tracer rules.
enum Function
{
	kTracerCreate = 24050,
	kTracerFree = 24051,
	kTracerClear = 24052,
	kLineTrace = 24055,
	kTraceHitCount = 24057,
	kTraceResult = 24058,
	kTraceResultStr = 24059,
	kLineInfo = 24060,
	kSectorInfo = 24061,
};

// [Doruk-Aksoy] The TRF_ flags at UZDoom's values, plus one of ours far above them.
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
const uint32_t kTraceNoSky = 1;
const uint32_t kTraceNoPassWidth = 8;
const uint32_t kFfUpperTexture = 0x20000;
const uint32_t kFfLowerTexture = 0x40000;

const size_t kMaxTracersPerScript = 16;
const size_t kMaxHits = 64;

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

// [Doruk-Aksoy] A tracer: its id, the result its last trace ended on, and the hits it kept.
struct Tracer
{
	int32_t id;
	Hit last;
	std::vector<Hit> hits;
};

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

	// [Doruk-Aksoy] Drops the tracer's hits and last result.
	bool Clear(int32_t id);

	// [Doruk-Aksoy] A trace's result: what it ended on, and the hits it kept, at most 64.
	bool SetResult(int32_t id, const Hit &last, const std::vector<Hit> &hits);

	size_t Count() const { return tracers.size(); }

	std::vector<Tracer> tracers;
};

// [Doruk-Aksoy] The largest id the script holds, 0 for none.
int32_t ComputeHighestTracerId(const ScriptTracers &tracers);

// [Doruk-Aksoy] The tracers as one run of ints for a savegame, and back; decoding refuses a damaged run.
std::vector<int32_t> ComputeFlattenTracers(const ScriptTracers &tracers);
bool ComputeUnflattenTracers(const int32_t *data, size_t count, ScriptTracers &tracers);

// [Doruk-Aksoy] Maps every texture name a tracer's hits hold through `map`.
template <class Map>
void ComputeRemapTracerNames(ScriptTracers &tracers, Map map)
{
	for (Tracer &t : tracers.tracers)
	{
		t.last.v[kTrHitTexture] = map(t.last.v[kTrHitTexture]);
		for (Hit &h : t.hits)
			h.v[kTrHitTexture] = map(h.v[kTrHitTexture]);
	}
}

}} // namespace zx::acstraces

#endif
