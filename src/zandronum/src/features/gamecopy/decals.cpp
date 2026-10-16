// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/decals.h"
#include "features/telemetry/telemetry.h"
#include "features/levelmesh/projdecals.h"   // [rc4l] CopyProjectedDecalOwners

#include "r_defs.h"
#include "r_state.h"

#include <vector>

namespace zx { namespace gamecopy {

namespace {

std::vector<DecalRecord> g_decals;
std::vector<int> g_first, g_count;   // [rc4l] each side's run in g_decals

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
	g_decals.clear();
	g_first.assign(numsides, 0);
	g_count.assign(numsides, 0);
	for (int i = 0; i < numsides; i++)
	{
		g_first[i] = (int)g_decals.size();
		for (DBaseDecal *d = sides[i].AttachedDecals; d != NULL; d = d->WallNext)
		{
			g_decals.emplace_back();
			DecalRecord &r = g_decals.back();
#define ZX_DECAL_FIELD(m) r.m = d->m;
#include "features/gamecopy/fields.h"
#undef ZX_DECAL_FIELD
		}
		g_count[i] = (int)g_decals.size() - g_first[i];
	}
	ZX_COUNT("copy/decals", "decals", g_decals.size());
	zx::levelmesh::CopyProjectedDecalOwners();
}

}} // namespace zx::gamecopy
