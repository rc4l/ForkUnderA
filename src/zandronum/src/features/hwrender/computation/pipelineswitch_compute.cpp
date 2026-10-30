// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/pipelineswitch_compute.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <utility>

namespace zx { namespace hwrender {

std::vector<PipelineSwitchCount> ComputePipelineSwitches(const std::vector<PipelineKey> &draws)
{
	std::map<std::pair<int, int>, int> pairs;
	for (size_t i = 1; i < draws.size(); i++)
		if (draws[i].pipeline != draws[i - 1].pipeline || draws[i].user != draws[i - 1].user)
			pairs[{ draws[i - 1].pipeline, draws[i].pipeline }]++;
	std::vector<PipelineSwitchCount> out;
	for (const auto &kv : pairs) out.push_back({ kv.first.first, kv.first.second, kv.second });
	std::stable_sort(out.begin(), out.end(), [](const PipelineSwitchCount &a, const PipelineSwitchCount &b) { return a.count > b.count; });
	return out;
}

int ComputeSwitchTotal(const std::vector<PipelineSwitchCount> &tally)
{
	int total = 0;
	for (const PipelineSwitchCount &s : tally) total += s.count;
	return total;
}

}} // namespace zx::hwrender
