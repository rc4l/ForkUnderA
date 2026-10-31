// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/seen.h"
#include "features/gamecopy/computation/seenmerge_compute.h"   // [Doruk-Aksoy] the sector and line bits a fill keeps
#include "features/handoff/seam.h"

#include "doomdata.h"
#include "r_defs.h"
#include "r_state.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace zx { namespace gamecopy {

namespace {

std::vector<unsigned char> g_bits[kSeenKinds];
std::vector<int> g_marked[kSeenKinds];   // [rc4l] what the walks marked since the last merge
std::vector<unsigned char> g_scanned;   // [rc4l] the live map's subsector bits as the tic ended
std::vector<std::pair<int, unsigned char>> g_scannedChanged[kSeenKinds];   // [Doruk-Aksoy] the live bits as the tic ended of the sectors and lines its end found changed

bool LiveSeen(SeenKind kind, int i)
{
	switch (kind)
	{
	case kSeenSubsector: return (subsectors[i].flags & SSECF_DRAWN) != 0;
	case kSeenSector: return (sectors[i].MoreFlags & SECF_DRAWN) != 0;
	default: return (lines[i].flags & ML_MAPPED) != 0;
	}
}

void SetLive(SeenKind kind, int i)
{
	switch (kind)
	{
	case kSeenSubsector: subsectors[i].flags |= SSECF_DRAWN; break;
	case kSeenSector: sectors[i].MoreFlags = WORD(sectors[i].MoreFlags | SECF_DRAWN); break;
	default: lines[i].flags |= ML_MAPPED; break;
	}
}

int CountOf(SeenKind kind)
{
	return kind == kSeenSubsector ? numsubsectors : kind == kSeenSector ? numsectors : numlines;
}

void TakeLive()
{
	for (int k = 0; k < kSeenKinds; k++)
	{
		const SeenKind kind = SeenKind(k);
		const int n = CountOf(kind);
		g_bits[k].resize(n);
		for (int i = 0; i < n; i++) g_bits[k][i] = LiveSeen(kind, i);
		g_marked[k].clear();
	}
}

} // namespace

bool Seen(SeenKind kind, int index)
{
	return index >= 0 && index < (int)g_bits[kind].size() && g_bits[kind][index] != 0;
}

void MarkSeen(SeenKind kind, int index)
{
	if (index < 0 || index >= (int)g_bits[kind].size() || g_bits[kind][index]) return;
	g_bits[kind][index] = 1;
	g_marked[kind].push_back(index);
}

void ApplySeen()
{
	ZX_SEAM("copy/seen", Handoff);
	for (int k = 0; k < kSeenKinds; k++)
	{
		for (int i : g_marked[k]) if (i < CountOf(SeenKind(k))) SetLive(SeenKind(k), i);
		g_marked[k].clear();
	}
}

void ScanSeen(const std::vector<int> &sectorsChanged, const std::vector<int> &linesChanged)
{
	g_scanned.resize(numsubsectors);
	for (int i = 0; i < numsubsectors; i++) g_scanned[i] = LiveSeen(kSeenSubsector, i);
	const std::vector<int> *changed[kSeenKinds] = { NULL, &sectorsChanged, &linesChanged };
	for (int k = kSeenSector; k < kSeenKinds; k++)
	{
		g_scannedChanged[k].clear();
		for (int i : *changed[k]) g_scannedChanged[k].push_back(std::make_pair(i, (unsigned char)LiveSeen(SeenKind(k), i)));
	}
}

int SeenMissed()
{
	int missed = 0;
	for (int k = 0; k < kSeenKinds; k++)
	{
		const SeenKind kind = SeenKind(k);
		const int n = CountOf(kind);
		for (int i = 0; i < n; i++) if (Seen(kind, i) != LiveSeen(kind, i)) missed++;
	}
	return missed;
}

void MergeSeen(bool scanned, std::vector<int> &sectorsSet, std::vector<int> &linesSet)
{
	sectorsSet = g_marked[kSeenSector];
	linesSet = g_marked[kSeenLine];
	bool whole = g_scanned.size() == (size_t)numsubsectors;
	for (int k = 0; k < kSeenKinds; k++) whole &= g_bits[k].size() == (size_t)CountOf(SeenKind(k));
	if (!scanned || !whole)
	{
		ApplySeen();
		TakeLive();
		return;
	}
	// [rc4l] The bits the tic's end read, with what the frames marked since, are the live map's once ApplySeen sets those.
	for (int i : g_marked[kSeenSubsector]) if (i >= 0 && i < (int)g_scanned.size()) g_scanned[i] = 1;
	g_bits[kSeenSubsector].swap(g_scanned);
	// [Doruk-Aksoy] A sector or line its end found unchanged is as the last fill took it, bit and all, so only the changed take their live bit.
	for (int k = kSeenSector; k < kSeenKinds; k++) ComputeSeenMerge(g_bits[k], g_scannedChanged[k], g_marked[k]);
	ApplySeen();
}

void InitSeen()
{
	TakeLive();
}

}} // namespace zx::gamecopy
