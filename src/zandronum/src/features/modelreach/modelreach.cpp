// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/modelreach/modelreach.h"
#include "features/modelreach/computation/modelreach_compute.h"
#include "features/gamecopy/actors.h"           // [Doruk-Aksoy] the records whose reach the fill took
#include "features/telemetry/telemetry.h"       // [Doruk-Aksoy] modelreach/prepare

#include "p_local.h"     // [Doruk-Aksoy] the blockmap
#include "r_defs.h"
#include "r_state.h"     // [Doruk-Aksoy] lines and sectors

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace zx { namespace modelreach {

namespace {

// [Doruk-Aksoy] One wide actor's footprint: its box and where its sectors start in g_sectors.
struct Footprint
{
	unsigned id;
	Box box;
	int first, count;
};

std::vector<int> g_wide;                // [Doruk-Aksoy] the wide records, ascending, each one's footprint at the same index in g_foot
std::vector<Footprint> g_foot, g_lastFoot;
std::vector<int> g_sectors, g_lastSectors;
std::unordered_map<unsigned, int> g_lastById;   // [Doruk-Aksoy] last fill's footprints by actor id, which a still actor's reuses
unsigned g_preparedFill = 0;
bool g_prepared = false;

std::vector<int> g_lines, g_found;
std::vector<FootLine> g_footLines;

std::vector<unsigned> g_stamps;         // [Doruk-Aksoy] per sector, the mark it last carried
unsigned g_stamp = 0;

bool SameBox(const Box &a, const Box &b)
{
	return a.left == b.left && a.bottom == b.bottom && a.right == b.right && a.top == b.top;
}

// [Doruk-Aksoy] The sectors under a box around a record standing in centreSector, from the blockmap's lines as the copy holds them.
void FindSectors(const Box &box, int centreSector)
{
	g_lines.clear();
	if (blockmap != NULL && blockmaplump != NULL)
		ComputeBlockmapLines(blockmap, blockmaplump, bmapwidth, bmapheight, FIXED2DBL(bmaporgx), FIXED2DBL(bmaporgy), MAPBLOCKUNITS, box, g_lines);
	g_footLines.resize(g_lines.size());
	for (size_t i = 0; i < g_lines.size(); i++)
	{
		const line_t &l = lines[g_lines[i]];
		FootLine &f = g_footLines[i];
		f.x1 = FIXED2DBL(l.v1->rc.x); f.y1 = FIXED2DBL(l.v1->rc.y);
		f.x2 = FIXED2DBL(l.v2->rc.x); f.y2 = FIXED2DBL(l.v2->rc.y);
		f.front = l.frontsector != NULL ? int(l.frontsector - sectors) : -1;
		f.back = l.backsector != NULL ? int(l.backsector - sectors) : -1;
	}
	ComputeFootprintSectors(g_footLines.empty() ? NULL : &g_footLines[0], (int)g_footLines.size(), box, centreSector, g_found);
}

} // namespace

void PrepareWide()
{
	const unsigned fill = zx::gamecopy::FillCount();
	if (g_prepared && fill == g_preparedFill) return;
	g_prepared = true;
	g_preparedFill = fill;
	ZX_ZONE("modelreach/prepare");   // [Doruk-Aksoy] the wide actors found and their sectors

	g_lastFoot.swap(g_foot);
	g_lastSectors.swap(g_sectors);
	g_lastById.clear();
	for (size_t i = 0; i < g_lastFoot.size(); i++) g_lastById[g_lastFoot[i].id] = int(i);
	g_foot.clear();
	g_sectors.clear();
	g_wide.clear();

	int count, wideCount;
	const zx::gamecopy::ActorRecord *records = zx::gamecopy::Actors(count);
	const int *wide = zx::gamecopy::WideActors(wideCount);
	for (int w = 0; w < wideCount; w++)
	{
		const int i = wide[w];
		if (i < 0 || i >= count) continue;
		const zx::gamecopy::ActorRecord &r = records[i];
		Footprint f;
		f.id = r.id;
		f.box = ComputeFootprintBox(FIXED2DBL(r.PrevX), FIXED2DBL(r.PrevY), FIXED2DBL(r.x), FIXED2DBL(r.y), r.modelFootprint);
		f.first = (int)g_sectors.size();
		const std::unordered_map<unsigned, int>::const_iterator last = g_lastById.find(r.id);
		if (last != g_lastById.end() && SameBox(g_lastFoot[last->second].box, f.box))
		{
			// [Doruk-Aksoy] An actor that has not moved stands over the sectors it stood over.
			const Footprint &was = g_lastFoot[last->second];
			g_sectors.insert(g_sectors.end(), g_lastSectors.begin() + was.first, g_lastSectors.begin() + was.first + was.count);
		}
		else
		{
			FindSectors(f.box, int(r.Sector - sectors));
			g_sectors.insert(g_sectors.end(), g_found.begin(), g_found.end());
		}
		f.count = (int)g_sectors.size() - f.first;
		g_foot.push_back(f);
		g_wide.push_back(i);
	}
	ZX_COUNT("modelreach/wide", "actors", g_wide.size());
}

const int *WideRecords(int &count)
{
	count = (int)g_wide.size();
	return g_wide.empty() ? NULL : &g_wide[0];
}

const int *FootprintSectors(int record, int &count)
{
	count = 0;
	const std::vector<int>::const_iterator at = std::lower_bound(g_wide.begin(), g_wide.end(), record);
	if (at == g_wide.end() || *at != record) return NULL;
	const Footprint &f = g_foot[at - g_wide.begin()];
	count = f.count;
	return f.count > 0 ? &g_sectors[f.first] : NULL;
}

void MarkReached(const int *list, int count)
{
	if (g_stamps.size() != (size_t)numsectors) g_stamps.assign(numsectors, 0);
	g_stamp++;
	for (int i = 0; i < count; i++)
		if (list[i] >= 0 && list[i] < numsectors) g_stamps[list[i]] = g_stamp;
}

void MarkReached(sector_t *const *list, int count)
{
	if (g_stamps.size() != (size_t)numsectors) g_stamps.assign(numsectors, 0);
	g_stamp++;
	for (int i = 0; i < count; i++) g_stamps[list[i] - sectors] = g_stamp;
}

bool FootprintReached(int record)
{
	int count;
	const int *list = FootprintSectors(record, count);
	return g_stamps.empty() ? false : ComputeAnyReached(list, count, &g_stamps[0], (int)g_stamps.size(), g_stamp);
}

bool Reached(const sector_t *sector)
{
	const ptrdiff_t i = sector != NULL ? sector - sectors : -1;
	return i >= 0 && i < (ptrdiff_t)g_stamps.size() && g_stamps[i] == g_stamp;
}

void EachWideMissed(void (*actor)(int record))
{
	int count;
	const zx::gamecopy::ActorRecord *records = zx::gamecopy::Actors(count);
	for (size_t i = 0; i < g_wide.size(); i++)
		if (g_wide[i] < count && !Reached(records[g_wide[i]].Sector) && FootprintReached(g_wide[i])) actor(g_wide[i]);
}

}} // namespace zx::modelreach
