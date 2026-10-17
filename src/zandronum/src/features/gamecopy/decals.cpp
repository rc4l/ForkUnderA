// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/decals.h"
#include "features/telemetry/telemetry.h"
#include "features/jobs/jobs.h"

#include "r_defs.h"
#include "r_state.h"

#include <algorithm>
#include <vector>

namespace zx { namespace gamecopy {

namespace {

std::vector<DecalRecord> g_decals;
std::vector<int> g_first, g_count;   // [rc4l] each side's run in g_decals
std::vector<std::vector<DecalRecord>> g_chunks;   // [rc4l] each pool chunk's decals, joined in side order
const int kChunk = 4096;   // [rc4l] sides a pool lane walks at a time
std::vector<int> g_scannedSides;   // [rc4l] the sides holding decals as the tic ended
std::vector<int> g_withDecals;     // [rc4l] the sides the last fill took decals from

// [rc4l] Side i's decals appended to out, as records.
void TakeSideDecals(int i, std::vector<DecalRecord> &out)
{
	for (DBaseDecal *d = sides[i].AttachedDecals; d != NULL; d = d->WallNext)
	{
		out.emplace_back();
		DecalRecord &r = out.back();
#define ZX_DECAL_FIELD(m) r.m = d->m;
#include "features/gamecopy/fields.h"
#undef ZX_DECAL_FIELD
	}
}

} // namespace

const DecalRecord *SideDecals(int side, int &count)
{
	count = side >= 0 && side < (int)g_count.size() ? g_count[side] : 0;
	return count > 0 ? &g_decals[g_first[side]] : NULL;
}

void DecalXY(const DecalRecord &decal, const side_t *side, fixed_t &x, fixed_t &y)
{
	const line_t *line = side->linedef;
	const vertex_t *v1 = line->sidedef[0] == side ? line->v1 : line->v2;
	const vertex_t *v2 = line->sidedef[0] == side ? line->v2 : line->v1;
	x = v1->rc.x + MulScale30(decal.LeftDistance, v2->rc.x - v1->rc.x);
	y = v1->rc.y + MulScale30(decal.LeftDistance, v2->rc.y - v1->rc.y);
}

void ScanDecalSides()
{
	g_scannedSides.clear();
	for (int i = 0; i < numsides; i++) if (sides[i].AttachedDecals != NULL) g_scannedSides.push_back(i);
}

int DecalSidesMissed()
{
	int missed = 0;
	for (int i = 0; i < numsides; i++)
		if (sides[i].AttachedDecals != NULL && !std::binary_search(g_scannedSides.begin(), g_scannedSides.end(), i)) missed++;
	return missed;
}

void FillDecals(bool scanned)
{
	ZX_ZONE("copy/decal_fill");   // [rc4l] the wall decals taken
	g_decals.clear();
	if (scanned && (int)g_count.size() == numsides)
	{
		// [rc4l] Only the sides the tic's end found holding decals, read live here, so one hit after it shows a tic later at worst.
		for (int i : g_withDecals) g_count[i] = 0;
		g_withDecals.clear();
		for (int i : g_scannedSides)
		{
			g_first[i] = (int)g_decals.size();
			TakeSideDecals(i, g_decals);
			g_count[i] = (int)g_decals.size() - g_first[i];
			if (g_count[i] > 0) g_withDecals.push_back(i);
		}
		ZX_COUNT("copy/decals", "decals", g_decals.size());
		return;
	}
	g_first.assign(numsides, 0);
	g_count.assign(numsides, 0);
	g_chunks.resize(zx::jobs::ComputeChunkCount(numsides, kChunk));
	// [rc4l] Walked across the job pool, since on Sunder MAP16 visiting 62,000 sides for a handful of decals was half a millisecond of every tic's handoff.
	auto take = [](int lo, int hi, int) {
		std::vector<DecalRecord> &out = g_chunks[lo / kChunk];
		out.clear();
		for (int i = lo; i < hi; i++)
		{
			const int before = (int)out.size();
			TakeSideDecals(i, out);
			g_count[i] = (int)out.size() - before;
		}
	};
	zx::jobs::Pool().ParallelChunks(numsides, kChunk, take);
	for (const std::vector<DecalRecord> &chunk : g_chunks) g_decals.insert(g_decals.end(), chunk.begin(), chunk.end());
	g_withDecals.clear();
	for (int i = 0, at = 0; i < numsides; i++)
	{
		g_first[i] = at;
		at += g_count[i];
		if (g_count[i] > 0) g_withDecals.push_back(i);
	}
	ZX_COUNT("copy/decals", "decals", g_decals.size());
}

}} // namespace zx::gamecopy
