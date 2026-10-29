// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Where a point's walk down a BSP tree can start: for each cell of a grid over the map, the deepest node every point in the cell reaches by the same path, so the walk skips the levels they all share and lands on the same subsector.
#ifndef ZX_BSPGRID_COMPUTE_H
#define ZX_BSPGRID_COMPUTE_H
#include <cstdint>
#include <vector>
namespace zx { namespace bspgrid {
// [Doruk-Aksoy] A node's partition in raw 48.16 fixed point, and its children: a node's index, or -1 - a subsector's index.
struct GridNode
{
	int64_t x, y, dx, dy;
	int child[2];
};
// [Doruk-Aksoy] R_PointOnSide's side of the partition, 1 for behind, as raw fixed point.
int ComputeNodeSide(int64_t x, int64_t y, const GridNode &node);
// [Doruk-Aksoy] Each cell's start: a node index, or -1 - a subsector's index when the whole cell lies in one; points outside start at root.
struct StartGrid
{
	int64_t originX = 0, originY = 0;
	int shift = 0, cols = 0, rows = 0, root = -1;
	std::vector<int> start;
};
// [Doruk-Aksoy] The grid over the box from (minX, minY) to (maxX, maxY), with cells of at least minShift bits of raw fixed point and at most maxCells of them.
void ComputeStartGrid(const std::vector<GridNode> &nodes, int root, int64_t minX, int64_t minY, int64_t maxX, int64_t maxY, int minShift,
	int maxCells, StartGrid &out);
// [Doruk-Aksoy] Where the walk for (x, y) starts.
int ComputeGridStart(const StartGrid &grid, int64_t x, int64_t y);
// [Doruk-Aksoy] The subsector (-1 - its index) a walk from node `from` lands on.
int ComputeWalk(const std::vector<GridNode> &nodes, int from, int64_t x, int64_t y);
// [Doruk-Aksoy] How many of each cell's corners and `interior` points from `seed` land elsewhere walking from the cell's start than from the root, for fua_bspgrid_check; points tested go to tested.
int ComputeGridMismatches(const std::vector<GridNode> &nodes, const StartGrid &grid, int interior, uint64_t seed, int &tested);
}} // namespace zx::bspgrid
#endif // ZX_BSPGRID_COMPUTE_H
