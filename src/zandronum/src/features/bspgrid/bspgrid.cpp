// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/bspgrid/bspgrid.h"
#include "features/bspgrid/computation/bspgrid_compute.h"
#include "features/telemetry/telemetry.h"

#include "r_defs.h"
#include "r_state.h"
#include "p_local.h"
#include "c_cvars.h"
#include "c_console.h"

#include <algorithm>
#include <climits>
#include <vector>

// [Doruk-Aksoy] At each level's load, every grid cell's corners and some points inside walked from its start and from the root, the landings compared and the differences printed.
CVAR(Bool, fua_bspgrid_check, false, 0)

namespace zx { namespace bspgrid {

namespace {

// [Doruk-Aksoy] 64 map units a cell at the finest, and at most a million cells a tree (4 MB).
const int kMinShift = 22;
const int kMaxCells = 1 << 20;

// [Doruk-Aksoy] A grid with the tree it was built from, which a lookup checks so a tree replaced since never uses it.
struct TreeGrid
{
	const node_t *tree = nullptr;
	int count = 0;
	StartGrid grid;
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

void Build(const char *which, const node_t *tree, int count, const subsector_t *subs, int64_t minX, int64_t minY, int64_t maxX, int64_t maxY, TreeGrid &out)
{
	std::vector<GridNode> converted;
	Convert(tree, count, subs, converted);
	ComputeStartGrid(converted, count - 1, minX, minY, maxX, maxY, kMinShift, kMaxCells, out.grid);
	out.tree = tree;
	out.count = count;
	if (fua_bspgrid_check)
	{
		int tested = 0;
		const int wrong = ComputeGridMismatches(converted, out.grid, 4, 1, tested);
		Printf("fua_bspgrid_check: %s tree, %d cells, %d points, %d landed elsewhere\n", which, out.grid.cols * out.grid.rows, tested, wrong);
	}
}

// [Doruk-Aksoy] Where a walk of tree's count nodes starts for (x, y): from the grid built for that tree, else from the root.
int StartIn(const TreeGrid &built, const node_t *tree, int count, fixed_t x, fixed_t y)
{
	if (built.tree != tree || built.count != count || tree == nullptr) return count - 1;
	return ComputeGridStart(built.grid, x.Raw(), y.Raw());
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
	Build("game", gamenodes, numgamenodes, gamesubsectors, minX, minY, maxX, maxY, g_game);
	// [Doruk-Aksoy] When the game walks the renderer's tree, both read the one grid.
	if (nodes != gamenodes) Build("render", nodes, numnodes, subsectors, minX, minY, maxX, maxY, g_render);
	ZX_COUNT("load/bspgrid_cells", "cells", (int)(g_game.grid.start.size() + g_render.grid.start.size()));
}

void ForgetLevel()
{
	g_game = TreeGrid();
	g_render = TreeGrid();
}

int GameStart(fixed_t x, fixed_t y)
{
	return StartIn(g_game, gamenodes, numgamenodes, x, y);
}

int RenderStart(fixed_t x, fixed_t y)
{
	return StartIn(nodes == gamenodes ? g_game : g_render, nodes, numnodes, x, y);
}

}} // namespace zx::bspgrid
