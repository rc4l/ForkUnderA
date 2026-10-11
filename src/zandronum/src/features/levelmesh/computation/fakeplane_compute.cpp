// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/fakeplane_compute.h"

#include <algorithm>

namespace zx { namespace levelmesh {

bool ComputeMissingUpper(const MissingTierInput &in, long long &planez)
{
	if (in.frontCeilSky && in.backCeilSky) return false;
	long long b0 = in.bch[0], b1 = in.bch[1];
	// [rc4l] The back floor rising past the back ceiling closes the gap at the front floor instead.
	if ((!in.frontFloorSky || !in.backFloorSky) && in.ffh[0] > in.bch[0] && in.ffh[1] > in.bch[1]) { b0 = in.ffh[0]; b1 = in.ffh[1]; }
	if (!(b0 < in.fch[0] || b1 < in.fch[1])) return false;
	if (in.textured || (in.sloped && !in.frontCeilSky && !in.backCeilSky)) return false;
	if (in.polyobj || !in.partnerOk) return false;
	planez = b0;
	return true;
}

bool ComputeMissingLower(const MissingTierInput &in, long long &planez)
{
	long long b0 = in.bfh[0], b1 = in.bfh[1];
	// [rc4l] The front ceiling coming down past the back floor closes the gap there instead.
	if (in.fch[0] < b0 && in.fch[1] < b1) { b0 = in.fch[0]; b1 = in.fch[1]; }
	if (!(b0 > in.ffh[0] || b1 > in.ffh[1])) return false;
	if (in.textured || (in.sloped && !in.frontFloorSky && !in.backFloorSky)) return false;
	if (in.backFloorSky || in.polyobj || !in.partnerOk) return false;
	planez = b0;
	return true;
}

void ComputeFileMissing(std::vector<MissingEntry> &entries, std::vector<int> &entryOfSub, bool upper, int sub, long long planez, int model)
{
	const int at = entryOfSub[(size_t)sub];
	if (at < 0)
	{
		entryOfSub[(size_t)sub] = (int)entries.size();
		entries.push_back({ sub, planez, model });
		return;
	}
	MissingEntry &e = entries[(size_t)at];
	if (upper ? planez < e.planez : planez > e.planez) { e.planez = planez; e.model = model; }
}

// [rc4l] DoOneSectorUpper and DoOneSectorLower as one recursion, check for check and in the same order, since which neighbour is reached first decides which edges get checked at all.
static bool Flood(bool upper, int sub, long long planez, const FillSeg *segs, const int *first, const int *count, int numSubs,
	std::vector<int> &seen, int stamp, std::vector<int> &out)
{
	for (int i = 0; i < count[sub]; i++)
		if (segs[first[sub] + i].closed) return false;
	for (int i = 0; i < count[sub]; i++)
	{
		const FillSeg &s = segs[first[sub] + i];
		if (s.backSub < 0 || s.backSub >= numSubs) return false;
		if (seen[s.backSub] == stamp) continue;
		seen[s.backSub] = stamp;
		if (s.sectorEdge)
		{
			if (upper ? s.farCeilSloped : s.farFloorSloped) return false;
			const long long z = upper ? s.farCeil : s.farFloor;
			if (upper ? z < planez : z > planez) return false;
			// [rc4l] A neighbour at exactly the fill's height bounds it, so long as no texture closes the gap there.
			if (z == planez)
			{
				if (upper ? s.topTextured : s.bottomTextured) return false;
				continue;
			}
		}
		if (!Flood(upper, s.backSub, planez, segs, first, count, numSubs, seen, stamp, out)) return false;
	}
	out.push_back(sub);
	return true;
}

// [rc4l] The visited marks are shared across fills with a fresh stamp each, as GL's validcount is: clearing one a subsector of the level for every entry cost milliseconds.
std::vector<int> ComputeFakePlaneFill(bool upper, int start, long long planez, const FillSeg *segs, const int *first, const int *count, int numSubs,
	std::vector<int> &seen, int stamp)
{
	std::vector<int> out;
	if (start < 0 || start >= numSubs) return out;
	seen[(size_t)start] = stamp;
	if (!Flood(upper, start, planez, segs, first, count, numSubs, seen, stamp, out)) out.clear();
	return out;
}

bool ComputeClosedAtFloor(bool sloped, long long floorZ, long long ceilingZ)
{
	return !sloped && floorZ > ceilingZ;
}

bool ComputeIsFillCandidate(bool hasSideAndLine, bool twoSided, bool horizonLine, bool subDrawnAsItsSector, bool frontIsSubSector, bool frontViewDependent)
{
	return hasSideAndLine && twoSided && !horizonLine && subDrawnAsItsSector && frontIsSubSector && !frontViewDependent;
}

std::vector<FakePlane> ComputeFakePlanes(bool upper, const std::vector<MissingEntry> &entries, const FillSeg *segs, const int *first,
	const int *count, int numSubs)
{
	std::vector<FakePlane> planes;
	std::vector<char> done(entries.size(), 0);
	std::vector<int> seen((size_t)std::max(numSubs, 0), 0);
	for (size_t i = 0; i < entries.size(); i++)
	{
		if (done[i]) continue;
		const std::vector<int> filled = ComputeFakePlaneFill(upper, entries[i].sub, entries[i].planez, segs, first, count, numSubs, seen, (int)i + 1);
		for (int s : filled) planes.push_back({ s, entries[i].model, upper });
		if (filled.size() < 2) continue;
		for (size_t k = 0; k < entries.size(); k++)
			for (int s : filled)
				if (entries[k].sub == s) done[k] = 1;
	}
	return planes;
}

void ComputeIndexByKey(const std::vector<int> &keys, int numKeys, std::vector<int> &start, std::vector<int> &order)
{
	start.assign((size_t)std::max(numKeys, 0) + 1, 0);
	for (int k : keys) start[(size_t)k + 1]++;
	for (int k = 0; k < numKeys; k++) start[(size_t)k + 1] += start[(size_t)k];
	std::vector<int> at(start.begin(), start.end() - 1);
	order.resize(keys.size());
	for (size_t i = 0; i < keys.size(); i++) order[(size_t)at[(size_t)keys[i]]++] = (int)i;
}

void ComputeGroupPlanes(const std::vector<FakePlane> &planes, int keys, bool byModel, std::vector<FakePlane> &out, std::vector<int> &start)
{
	std::vector<int> key(planes.size()), order;
	for (size_t i = 0; i < planes.size(); i++) key[i] = byModel ? planes[i].model : planes[i].sub;
	ComputeIndexByKey(key, keys, start, order);
	out.resize(planes.size());
	for (size_t i = 0; i < order.size(); i++) out[i] = planes[(size_t)order[i]];
}

std::vector<int> ComputeChangedSubs(const std::vector<FakePlane> &before, const std::vector<int> &beforeStart,
	const std::vector<FakePlane> &now, const std::vector<int> &nowStart, int numSubs)
{
	std::vector<int> changed;
	const bool had = beforeStart.size() == nowStart.size();
	for (int i = 0; i < numSubs; i++)
	{
		const int a0 = had ? beforeStart[(size_t)i] : 0, a1 = had ? beforeStart[(size_t)i + 1] : 0;
		const int b0 = nowStart[(size_t)i], b1 = nowStart[(size_t)i + 1];
		bool same = (a1 - a0) == (b1 - b0);
		for (int k = 0; same && k < b1 - b0; k++)
			same = before[(size_t)(a0 + k)].model == now[(size_t)(b0 + k)].model && before[(size_t)(a0 + k)].upper == now[(size_t)(b0 + k)].upper;
		if (!same) changed.push_back(i);
	}
	return changed;
}

}} // namespace zx::levelmesh
