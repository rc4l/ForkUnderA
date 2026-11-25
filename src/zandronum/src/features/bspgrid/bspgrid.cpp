// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/bspgrid/bspgrid.h"
#include "features/bspgrid/computation/bspgrid_compute.h"
#include "features/telemetry/telemetry.h"

#include "r_defs.h"
#include "r_state.h"
#include "r_utility.h"
#include "p_local.h"
#include "c_cvars.h"
#include "c_console.h"

#include <algorithm>
#include <climits>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace zx { namespace bspgrid {

namespace {

// [Doruk-Aksoy] 64 map units a cell at the finest, and at most a million cells a tree (4 MB).
const int kMinShift = 22;
const int kMaxCells = 1 << 20;

// [Doruk-Aksoy] A grid with the tree it was built from, which a lookup checks so a tree replaced since never uses it, and the tree's nodes in preorder that the grid's starts index.
struct TreeGrid
{
	const node_t *tree = nullptr;
	int count = 0;
	subsector_t *subs = nullptr;
	StartGrid grid;
	std::vector<GridNode> walk;
};

TreeGrid g_game, g_render;

// [Doruk-Aksoy] A tree's nodes as the grid reads them, each child a node's index or -1 - a subsector's.
void Convert(const node_t *tree, int count, const subsector_t *subs, std::vector<GridNode> &out)
{
	out.resize(count);
	for (int i = 0; i < count; i++)
	{
		const node_t &n = tree[i];
		out[i].x = n.x.Raw();
		out[i].y = n.y.Raw();
		out[i].dx = n.dx.Raw();
		out[i].dy = n.dy.Raw();
		for (int k = 0; k < 2; k++)
		{
			const size_t child = (size_t)n.children[k];
			out[i].child[k] = (child & 1) ? -1 - int((const subsector_t *)(child - 1) - subs) : int((const node_t *)child - tree);
		}
	}
}

void Build(const node_t *tree, int count, subsector_t *subs, int64_t minX, int64_t minY, int64_t maxX, int64_t maxY, TreeGrid &out)
{
	std::vector<GridNode> converted;
	Convert(tree, count, subs, converted);
	ComputeStartGrid(converted, count - 1, minX, minY, maxX, maxY, kMinShift, kMaxCells, out.grid);
	std::vector<int> newIndex;
	ComputePreorder(converted, count - 1, out.walk, newIndex);
	ComputeRenumberGrid(out.grid, newIndex);
	out.tree = tree;
	out.count = count;
	out.subs = subs;
}

// [Doruk-Aksoy] The subsector at (x, y) walking the compact copy from its grid's start with R_PointOnSide's test, or NULL when the grid was not built for this tree.
subsector_t *WalkIn(const TreeGrid &built, const node_t *tree, int count, fixed_t x, fixed_t y)
{
	if (built.tree != tree || built.count != count || tree == nullptr || built.walk.empty()) return nullptr;
	int at = ComputeGridStart(built.grid, x.Raw(), y.Raw());
	while (at >= 0)
	{
		const GridNode &n = built.walk[(size_t)at];
		at = n.child[R_PointOnPartition(x, y, fixed_t::FromRaw(n.x), fixed_t::FromRaw(n.y), fixed_t::FromRaw(n.dx), fixed_t::FromRaw(n.dy))];
	}
	return built.subs + (-1 - at);
}

} // namespace

void BuildForLevel()
{
	ZX_ZONE("load/bspgrid");
	ForgetLevel();
	if (numvertexes <= 0 || numgamenodes <= 0 || numnodes <= 0) return;
	int64_t minX = INT64_MAX, minY = INT64_MAX, maxX = INT64_MIN, maxY = INT64_MIN;
	for (int i = 0; i < numvertexes; i++)
	{
		minX = std::min(minX, vertexes[i].x.Raw());
		minY = std::min(minY, vertexes[i].y.Raw());
		maxX = std::max(maxX, vertexes[i].x.Raw());
		maxY = std::max(maxY, vertexes[i].y.Raw());
	}
	Build(gamenodes, numgamenodes, gamesubsectors, minX, minY, maxX, maxY, g_game);
	// [Doruk-Aksoy] When the game walks the renderer's tree, both read the one grid.
	if (nodes != gamenodes) Build(nodes, numnodes, subsectors, minX, minY, maxX, maxY, g_render);
	ZX_COUNT("load/bspgrid_cells", "cells", (int)(g_game.grid.start.size() + g_render.grid.start.size()));
}

void ForgetLevel()
{
	g_game = TreeGrid();
	g_render = TreeGrid();
}

subsector_t *GameSubsector(fixed_t x, fixed_t y)
{
	return WalkIn(g_game, gamenodes, numgamenodes, x, y);
}

subsector_t *RenderSubsector(fixed_t x, fixed_t y)
{
	return WalkIn(nodes == gamenodes ? g_game : g_render, nodes, numnodes, x, y);
}

}} // namespace zx::bspgrid
