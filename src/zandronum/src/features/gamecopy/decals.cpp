// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/decals.h"
#include "features/telemetry/telemetry.h"
#include "features/jobs/jobs.h"

#include "r_defs.h"
#include "r_state.h"

#include <vector>

namespace zx { namespace gamecopy {

namespace {

std::vector<DecalRecord> g_decals;
std::vector<int> g_first, g_count;   // [rc4l] each side's run in g_decals
std::vector<std::vector<DecalRecord>> g_chunks;   // [rc4l] each pool chunk's decals, joined in side order
const int kChunk = 4096;   // [rc4l] sides a pool lane walks at a time

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

void FillDecals()
{
	ZX_ZONE("copy/decal_fill");   // [rc4l] the wall decals taken
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
			for (DBaseDecal *d = sides[i].AttachedDecals; d != NULL; d = d->WallNext)
			{
				out.emplace_back();
				DecalRecord &r = out.back();
#define ZX_DECAL_FIELD(m) r.m = d->m;
#include "features/gamecopy/fields.h"
#undef ZX_DECAL_FIELD
			}
			g_count[i] = (int)out.size() - before;
		}
	};
	zx::jobs::Pool().ParallelChunks(numsides, kChunk, take);
	g_decals.clear();
	for (const std::vector<DecalRecord> &chunk : g_chunks) g_decals.insert(g_decals.end(), chunk.begin(), chunk.end());
	for (int i = 0, at = 0; i < numsides; i++) { g_first[i] = at; at += g_count[i]; }
	ZX_COUNT("copy/decals", "decals", g_decals.size());
}

}} // namespace zx::gamecopy
