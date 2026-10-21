// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The map's change scans found as a tic ends, on the thread that ran it, so the handoff takes only what changed instead of comparing the whole map while every frame waits.

#ifndef ZX_PRESCAN_COMPUTE_H
#define ZX_PRESCAN_COMPUTE_H

#include <vector>

namespace zx { namespace gamecopy {

// [rc4l] When a scan was found: for which fill of which level, after how many main-thread effects and thinker or sector list changes, over a map of which sizes.
struct PrescanStamp
{
	bool found;
	unsigned fill;
	int level;
	unsigned effects, lists;
	int sectors, sides, lines;
};

// [rc4l] Whether a scan still says what the handoff must take: found for this fill of this level at the map's sizes, with no effect run since that could have changed the map, and not on a client, whose map changes between tics.
bool ComputePrescanFresh(const PrescanStamp &found, const PrescanStamp &now, bool client);

// [rc4l] Whether the actor list and sector links a scan made still hold: the scan is fresh and no thinker or actor joined or left a list since, which the medals ticked after the playsim do.
bool ComputeActorScanFresh(const PrescanStamp &found, const PrescanStamp &now, bool client);

// [rc4l] The items the handoff compares and takes: what the tic changed, what the frames blended and what they marked seen, each once, in order, and within count.
void ComputeTakeCandidates(const std::vector<int> &changed, const std::vector<int> &blended, const std::vector<int> &seen, int count,
	std::vector<int> &out);

}} // namespace zx::gamecopy

#endif // ZX_PRESCAN_COMPUTE_H
