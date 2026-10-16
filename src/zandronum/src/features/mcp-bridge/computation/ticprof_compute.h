// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The arithmetic of the bridge's perf.ticprof (mcp_ticprof.cpp): each tic's sim phases are what features/telemetry's sim zones gathered between two tics, reported in the event's own JSON.

#ifndef ZX_TICPROF_COMPUTE_H
#define ZX_TICPROF_COMPUTE_H

#include <cstdint>
#include <string>
#include <vector>

namespace zx { namespace mcp {

// [rc4l] The phases a tic is split into, in the report's order: G_Ticker whole, P_Ticker, the thinkers, the effects and the specials.
const int kTicZoneCount = 5;
// [rc4l] Each phase's ZX_ZONE name, which its site in d_net.cpp, g_game.cpp or p_tick.cpp must keep.
extern const char *const kTicZones[kTicZoneCount];

// [rc4l] One tic's phases in milliseconds.
struct TicPhases { double ms[kTicZoneCount]; };

// [rc4l] Each phase's milliseconds between two readings of its nanosecond totals.
TicPhases ComputeTicPhases(const int64_t *before, const int64_t *after);

// [rc4l] The "ticprof" event's data: every tic's phases under the keys the bridge has always sent.
std::string ComputeTicProfJson(const std::vector<TicPhases> &tics);

}} // namespace zx::mcp

#endif // ZX_TICPROF_COMPUTE_H
