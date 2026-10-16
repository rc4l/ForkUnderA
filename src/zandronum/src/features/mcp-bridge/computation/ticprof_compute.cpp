// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/ticprof_compute.h"

namespace zx { namespace mcp {

const char *const kTicZones[kTicZoneCount] = { "sim/gticker", "sim/pticker", "sim/thinkers", "sim/effects", "sim/specials" };

namespace {

// [rc4l] The report's key for each phase, in kTicZones' order.
const char *const kTicKeys[kTicZoneCount] = { "total", "pticker", "thinkers", "effects", "specials" };

} // namespace

TicPhases ComputeTicPhases(const int64_t *before, const int64_t *after)
{
	TicPhases t;
	for (int z = 0; z < kTicZoneCount; z++) t.ms[z] = (after[z] - before[z]) / 1e6;
	return t;
}

std::string ComputeTicProfJson(const std::vector<TicPhases> &tics)
{
	std::string out = "{\"tics\":[";
	for (size_t i = 0; i < tics.size(); i++)
	{
		out += i ? ",{" : "{";
		for (int z = 0; z < kTicZoneCount; z++) out += std::string(z ? "," : "") + "\"" + kTicKeys[z] + "\":" + std::to_string(tics[i].ms[z]);
		out += "}";
	}
	return out + "]}";
}

}} // namespace zx::mcp
