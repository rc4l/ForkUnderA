// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which files a server's advertised WAD set actually asks us to load.
//
// A server reports its IWAD and its PWAD list separately, but the two are not guaranteed disjoint and
// the list is not guaranteed clean: a server can repeat a file, report the IWAD among its PWADs, or
// send an empty entry. Handing that to the loader verbatim means asking for the same file twice or
// loading the IWAD as a mod, so the list is normalised first -- and normalising is a decision worth
// testing off-engine rather than a loop buried in the join path.
//
// Order is preserved. WAD load order is semantic in Doom: later files override earlier ones, so the
// server's ordering IS the mod, and reordering it silently changes what the game is.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_JOINPLAN_COMPUTE_H
#define ZX_JOINPLAN_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// `iwad` may be empty. Returns the PWADs to request, in the server's order, with:
//   - empty / whitespace-only entries dropped (a server that pads its list should not fail the join)
//   - the IWAD dropped if it also appears among the PWADs (it is loaded as the IWAD, not as a mod)
//   - later duplicates dropped, keeping the FIRST occurrence
//
// Comparison is case-insensitive: WAD names cross platforms, and a server on Linux advertising
// "Brutal.wad" must match a local "brutal.wad" or the join fails over nothing.
std::vector<std::string> ComputeJoinWadList(const std::string &iwad,
	const std::vector<std::string> &pwads);

} // namespace zx

#endif // ZX_JOINPLAN_COMPUTE_H
