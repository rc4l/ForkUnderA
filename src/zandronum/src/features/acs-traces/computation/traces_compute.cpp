// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-traces/computation/traces_compute.h"

#include <algorithm>

#include "features/fixed64/computation/fixed64_scale_compute.h"

namespace zx { namespace acstraces {

namespace {

int64_t Mul16(int64_t a, int64_t b)
{
	return MulScale64(a, b, 16);
}

// [Doruk-Aksoy] Engine fixed point cut to ACS fixed point, as GetActorX cuts it.
int32_t ToAcs(int64_t raw)
{
	return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(raw)));
}

} // namespace

TraceMasks ComputeLineTraceMasks(int flags, const Shooter &shooter)
{
	TraceMasks m;
	m.actors = (flags & kTrfAllActors) ? 0xFFFFFFFFu : kMfShootable;
	if (flags & kTrfSolidActors)
		m.actors |= kMfSolid;
	m.lines = kMlBlockEverything | kMlBlockHitscan;
	if (flags & kTrfBlockUse)
		m.lines |= kMlBlockUse;
	if (flags & kTrfBlockSelf)
	{
		// [Doruk-Aksoy] UZDoom's test, less MF8_BLOCKASPLAYER, which we don't have.
		if (shooter.projectile)
			m.lines |= kMlBlockProjectile;
		else
			m.lines |= kMlBlocking;
		if (!shooter.notBlocked)
			m.lines |= kMlBlockMonsters;
		if (shooter.player)
			m.lines |= kMlBlocking | kMlBlockPlayers;
		if (shooter.floater)
			m.lines |= kMlBlockFloaters;
	}
	if (flags & kTrfThruBlock)
		m.lines &= ~kMlBlockEverything;
	if (flags & kTrfThruHitscan)
		m.lines &= ~kMlBlockHitscan;
	m.trace = 0;
	if (flags & kTrfNoSky)
		m.trace |= kTraceNoSky;
	if (flags & kTrfFuaPhysicalSize)
		m.trace |= kTraceNoPassWidth;
	return m;
}

uint32_t ComputeAcsAngleToFine(int32_t angle)
{
	return (static_cast<uint32_t>(angle) << 16) >> 19;
}

Fixed3 ComputeTraceDirection(int64_t cosPitch, int64_t sinPitch, int64_t cosAngle, int64_t sinAngle)
{
	return Fixed3{ Mul16(cosPitch, cosAngle), Mul16(cosPitch, sinAngle), -sinPitch };
}

Fixed3 ComputeLineTraceStart(int flags, const Fixed3 &actor, int64_t floorclip, int64_t cosAngle, int64_t sinAngle, int32_t offsetZ, int32_t offsetForward, int32_t offsetSide)
{
	if (flags & kTrfAbsPosition)
		return Fixed3{ offsetForward, offsetSide, offsetZ };
	const int64_t z = actor.z - floorclip + offsetZ;
	if (flags & kTrfAbsOffset)
		return Fixed3{ actor.x + offsetForward, actor.y + offsetSide, z };
	return Fixed3{ actor.x + Mul16(offsetForward, cosAngle) + Mul16(offsetSide, sinAngle),
		actor.y + Mul16(offsetForward, sinAngle) - Mul16(offsetSide, cosAngle), z };
}

TraceStatus ComputeLineTraceDecision(int type, bool thruActors, bool thruSpecies, bool sameSpecies)
{
	if (type != kHitActor)
		return kTraceStop;
	return thruActors || (thruSpecies && sameSpecies) ? kTraceSkip : kTraceStop;
}

int ComputeLinePart(int tier)
{
	switch (tier)
	{
	case kTierUpper: return 0;
	case kTierLower: return 2;
	default: return 1;
	}
}

int ComputeWallTextureSlot(int tier, uint32_t ffloorFlags)
{
	if (tier != kTierFFloor)
		return ComputeLinePart(tier);
	return (ffloorFlags & kFfUpperTexture) ? 0 : (ffloorFlags & kFfLowerTexture) ? 2 : 1;
}

Hit ComputeTraceHit(const RawHit &raw)
{
	Hit h = ComputeEmptyHit();
	const bool wall = raw.type == kHitWall;
	h.v[kTrHitType] = raw.type;
	h.v[kTrHitActor] = raw.type == kHitActor ? raw.actor : 0;
	h.v[kTrHitX] = ToAcs(raw.pos.x);
	h.v[kTrHitY] = ToAcs(raw.pos.y);
	h.v[kTrHitZ] = ToAcs(raw.pos.z);
	h.v[kTrDirX] = ToAcs(raw.dir.x);
	h.v[kTrDirY] = ToAcs(raw.dir.y);
	h.v[kTrDirZ] = ToAcs(raw.dir.z);
	h.v[kTrDistance] = ToAcs(raw.distance);
	h.v[kTrFraction] = ToAcs(raw.fraction);
	h.v[kTrHitLine] = wall ? raw.line : -1;
	h.v[kTrLineId] = wall ? raw.lineId : 0;
	h.v[kTrLineSide] = wall ? raw.side : 0;
	h.v[kTrLinePart] = wall ? ComputeLinePart(raw.tier) : 0;
	h.v[kTrHitSector] = raw.sector;
	h.v[kTrSectorTag] = raw.sector >= 0 ? raw.sectorTag : 0;
	h.v[kTrSectorPlane] = raw.type == kHitCeiling ? 1 : 0;
	h.v[kTr3DFloor] = wall && raw.tier == kTierFFloor ? raw.ffloorSector : -1;
	h.v[kTrCrossedWater] = raw.crossedWater;
	h.v[kTrCrossed3DWater] = raw.crossed3DWater;
	h.v[kTrHitTexture] = raw.type == kHitActor ? 0 : raw.texture;
	return h;
}

Hit ComputeEmptyHit()
{
	Hit h = {};
	h.v[kTrHitLine] = -1;
	h.v[kTrHitSector] = -1;
	h.v[kTr3DFloor] = -1;
	h.v[kTrCrossedWater] = -1;
	h.v[kTrCrossed3DWater] = -1;
	return h;
}

bool ComputeTracePropValid(int prop)
{
	return prop >= 0 && prop < kTrPropCount;
}

int32_t ComputeTraceProp(const Hit &hit, int prop)
{
	return ComputeTracePropValid(prop) && prop != kTrHitTexture ? hit.v[prop] : 0;
}

int32_t ComputeInfoProp(const int32_t *values, int count, int prop)
{
	return prop >= 0 && prop < count ? values[prop] : 0;
}

const Hit *ComputeTracerHit(const Tracer &tracer, int index)
{
	if (index == -1)
		return &tracer.last;
	return index >= 0 && size_t(index) < tracer.hits.size() ? &tracer.hits[size_t(index)] : nullptr;
}

bool ScriptTracers::Create(int32_t id)
{
	if (tracers.size() >= kMaxTracersPerScript || Find(id) != nullptr)
		return false;
	tracers.push_back(Tracer{ id, ComputeEmptyHit(), {} });
	return true;
}

Tracer *ScriptTracers::Find(int32_t id)
{
	return const_cast<Tracer *>(static_cast<const ScriptTracers *>(this)->Find(id));
}

const Tracer *ScriptTracers::Find(int32_t id) const
{
	if (id == 0)
		return nullptr;
	for (const Tracer &t : tracers)
		if (t.id == id)
			return &t;
	return nullptr;
}

bool ScriptTracers::Free(int32_t id)
{
	const auto at = std::find_if(tracers.begin(), tracers.end(), [id](const Tracer &t) { return t.id == id && id != 0; });
	if (at == tracers.end())
		return false;
	tracers.erase(at);
	return true;
}

bool ScriptTracers::Clear(int32_t id)
{
	return SetResult(id, ComputeEmptyHit(), {});
}

bool ScriptTracers::SetResult(int32_t id, const Hit &last, const std::vector<Hit> &hits)
{
	Tracer *t = Find(id);
	if (t == nullptr)
		return false;
	t->last = last;
	t->hits.assign(hits.begin(), hits.begin() + std::min(hits.size(), kMaxHits));
	return true;
}

int32_t ComputeHighestTracerId(const ScriptTracers &tracers)
{
	int32_t highest = 0;
	for (const Tracer &t : tracers.tracers)
		highest = std::max(highest, t.id);
	return highest;
}

std::vector<int32_t> ComputeFlattenTracers(const ScriptTracers &tracers)
{
	std::vector<int32_t> out;
	out.push_back(int32_t(tracers.tracers.size()));
	for (const Tracer &t : tracers.tracers)
	{
		out.push_back(t.id);
		out.insert(out.end(), t.last.v, t.last.v + kTrPropCount);
		out.push_back(int32_t(t.hits.size()));
		for (const Hit &h : t.hits)
			out.insert(out.end(), h.v, h.v + kTrPropCount);
	}
	return out;
}

bool ComputeUnflattenTracers(const int32_t *data, size_t count, ScriptTracers &tracers)
{
	size_t at = 0;
	// [Doruk-Aksoy] The next int, if one is left and it lies in [lo, hi].
	const auto take = [&](int32_t lo, int32_t hi, int32_t &value) {
		if (at >= count)
			return false;
		value = data[at++];
		return value >= lo && value <= hi;
	};
	// [Doruk-Aksoy] The next hit, if a whole one is left.
	const auto hit = [&](Hit &h) {
		if (count - at < size_t(kTrPropCount))
			return false;
		std::copy(data + at, data + at + kTrPropCount, h.v);
		at += kTrPropCount;
		return true;
	};
	ScriptTracers read;
	int32_t n;
	if (!take(0, int32_t(kMaxTracersPerScript), n))
		return false;
	for (int32_t i = 0; i < n; ++i)
	{
		int32_t id, hits;
		Hit last;
		if (!take(1, INT32_MAX, id) || !read.Create(id) || !hit(last) || !take(0, int32_t(kMaxHits), hits))
			return false;
		std::vector<Hit> kept(size_t(hits), Hit{});
		for (Hit &h : kept)
			if (!hit(h))
				return false;
		read.SetResult(id, last, kept);
	}
	if (at != count)
		return false;
	tracers = read;
	return true;
}

}} // namespace zx::acstraces
