// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/lighttouch_compute.h"

#include <algorithm>

namespace zx { namespace hwrender {

void ComputeDynLightColor(int r, int g, int b, bool additive, float intensity, float out[3])
{
	const float scale = (additive ? 0.2f : 1.0f) * intensity;
	out[0] = r / 255.0f * scale;
	out[1] = g / 255.0f * scale;
	out[2] = b / 255.0f * scale;
}

bool ComputeLightsDue(LightsStamp &kept, int tic, int generation, int flags, bool &newLevel)
{
	newLevel = generation != kept.generation;
	if (tic == kept.tic && !newLevel && flags == kept.flags) return false;
	kept.tic = tic;
	kept.generation = generation;
	kept.flags = flags;
	return true;
}

bool ComputeLightLinksStale(const LightLinkKey &kept, const LightLinkKey &now)
{
	return kept.light != now.light || kept.x != now.x || kept.y != now.y || kept.z != now.z || kept.radius != now.radius;
}

void ComputeSortedTouches(std::vector<unsigned> &touched)
{
	std::sort(touched.begin(), touched.end());
	touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
}

void ComputeLightTouchLists(const std::vector<const std::vector<unsigned> *> &perLight, unsigned capacity,
	std::vector<LightTouchRange> &ranges, std::vector<unsigned> &ids)
{
	ranges.clear();
	ids.clear();
	for (const std::vector<unsigned> *sorted : perLight)
	{
		LightTouchRange r;
		if (ids.size() + sorted->size() > capacity) { r.offset = 0; r.count = kLightTouchesAll; }
		else
		{
			r.offset = (unsigned)ids.size();
			r.count = (unsigned)sorted->size();
			ids.insert(ids.end(), sorted->begin(), sorted->end());
		}
		ranges.push_back(r);
	}
}

bool ComputeLightTouches(const std::vector<unsigned> &ids, const LightTouchRange &r, unsigned id)
{
	if (r.count == kLightTouchesAll) return true;
	unsigned lo = r.offset, hi = r.offset + r.count;
	while (lo < hi)
	{
		const unsigned mid = (lo + hi) >> 1;
		if (ids[mid] == id) return true;
		if (ids[mid] < id) lo = mid + 1; else hi = mid;
	}
	return false;
}

}} // namespace zx::hwrender
