// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/slotreport_compute.h"

#include <algorithm>
#include <cstddef>

namespace zx { namespace hwrender {

void ComputeWorkingSet(const std::vector<SlotStamp> &stamps, long long frame, double nowMs, const std::vector<double> &windowsMs, std::vector<int> &counts)
{
	counts.resize(windowsMs.size() + 1, 0);
	for (const SlotStamp &s : stamps)
	{
		if (s.frame < 0) continue;
		if (s.frame == frame) counts[0]++;
		for (size_t w = 0; w < windowsMs.size(); w++)
			if (nowMs - s.ms <= windowsMs[w]) counts[w + 1]++;
	}
}

void ComputeKeepPeaks(std::vector<int> &peaks, const std::vector<int> &counts)
{
	if (peaks.size() < counts.size()) peaks.resize(counts.size(), 0);
	for (size_t i = 0; i < counts.size(); i++) peaks[i] = std::max(peaks[i], counts[i]);
}

}} // namespace zx::hwrender
