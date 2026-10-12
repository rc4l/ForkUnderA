// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/lighttouch_compute.h"

#include <algorithm>

namespace zx { namespace hwrender {

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
