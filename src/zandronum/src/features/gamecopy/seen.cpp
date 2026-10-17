// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/seen.h"
#include "features/handoff/seam.h"

#include "doomdata.h"
#include "r_defs.h"
#include "r_state.h"

#include <vector>

namespace zx { namespace gamecopy {

namespace {

std::vector<unsigned char> g_bits[kSeenKinds];
std::vector<int> g_marked[kSeenKinds];   // [rc4l] what the walks marked since the last merge
std::vector<unsigned char> g_scanned[kSeenKinds];   // [rc4l] the live map's bits as the tic ended

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

void ScanSeen()
{
	for (int k = 0; k < kSeenKinds; k++)
	{
		const SeenKind kind = SeenKind(k);
		const int n = CountOf(kind);
		g_scanned[k].resize(n);
		for (int i = 0; i < n; i++) g_scanned[k][i] = LiveSeen(kind, i);
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
	bool whole = true;
	for (int k = 0; k < kSeenKinds; k++) whole &= g_scanned[k].size() == (size_t)CountOf(SeenKind(k));
	if (!scanned || !whole)
	{
		ApplySeen();
		TakeLive();
		return;
	}
	// [rc4l] The bits the tic's end read, with what the frames marked since, are the live map's once ApplySeen sets those.
	for (int k = 0; k < kSeenKinds; k++)
		for (int i : g_marked[k]) if (i >= 0 && i < (int)g_scanned[k].size()) g_scanned[k][i] = 1;
	ApplySeen();
	for (int k = 0; k < kSeenKinds; k++) g_bits[k].swap(g_scanned[k]);
}

void InitSeen()
{
	TakeLive();
}

}} // namespace zx::gamecopy
