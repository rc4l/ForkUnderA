// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/bspgrid/computation/bspgrid_compute.h"
#include "features/fixed64/computation/fixed64_scale_compute.h"

#include <climits>
#include <cstddef>

namespace zx { namespace bspgrid {

int ComputeNodeSide(int64_t x, int64_t y, const GridNode &node)
{
	return zx::DMulScale64(y - node.y, node.dx, node.x - x, node.dy, 32) > 0;
}

namespace {

// [Doruk-Aksoy] Whether v is small enough that a product of two such and their sum shifted down stay exact in 64 bits.
bool Small(int64_t v)
{
	const int64_t limit = (int64_t)1 << 46;
	return v > -limit && v < limit;
}

// [Doruk-Aksoy] The side test is a threshold on a function linear in x and y, so when a box's four corners agree every point in it does, while the arithmetic stays exact: never at a node whose int32 product sum can overflow (dx and dy both INT32_MIN), nor past 2^46.
bool CornersAgree(const GridNode &node, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int &side)
{
	if (node.dx == INT32_MIN && node.dy == INT32_MIN) return false;
	if (!Small(node.dx) || !Small(node.dy) || !Small(x0 - node.x) || !Small(x1 - node.x) || !Small(y0 - node.y) || !Small(y1 - node.y)) return false;
	side = ComputeNodeSide(x0, y0, node);
	return ComputeNodeSide(x1, y0, node) == side && ComputeNodeSide(x0, y1, node) == side && ComputeNodeSide(x1, y1, node) == side;
}

// [Doruk-Aksoy] Cells c0..c1 by r0..r1 (exclusive) walked down together from node `from` while their corners agree, then each quarter on its own.
void Fill(const std::vector<GridNode> &nodes, StartGrid &grid, int c0, int r0, int c1, int r1, int from)
{
	const int64_t x0 = grid.originX + ((int64_t)c0 << grid.shift), x1 = grid.originX + ((int64_t)c1 << grid.shift) - 1;
	const int64_t y0 = grid.originY + ((int64_t)r0 << grid.shift), y1 = grid.originY + ((int64_t)r1 << grid.shift) - 1;
	int at = from, side;
	while (at >= 0 && CornersAgree(nodes[at], x0, y0, x1, y1, side)) at = nodes[at].child[side];
	if (at < 0 || (c1 - c0 == 1 && r1 - r0 == 1))
	{
		for (int r = r0; r < r1; r++)
			for (int c = c0; c < c1; c++) grid.start[(size_t)r * grid.cols + c] = at;
		return;
	}
	const int cm = c1 - c0 > 1 ? (c0 + c1) / 2 : c1, rm = r1 - r0 > 1 ? (r0 + r1) / 2 : r1;
	Fill(nodes, grid, c0, r0, cm, rm, at);
	if (cm < c1) Fill(nodes, grid, cm, r0, c1, rm, at);
	if (rm < r1) Fill(nodes, grid, c0, rm, cm, r1, at);
	if (cm < c1 && rm < r1) Fill(nodes, grid, cm, rm, c1, r1, at);
}

} // namespace

void ComputeStartGrid(const std::vector<GridNode> &nodes, int root, int64_t minX, int64_t minY, int64_t maxX, int64_t maxY, int minShift,
	int maxCells, StartGrid &out)
{
	out = StartGrid();
	out.root = root;
	// [Doruk-Aksoy] A box past 2^55 raw (half a billion map units) gets no grid, so no cell's corners can overflow.
	const int64_t far = (int64_t)1 << 55;
	if (root < 0 || root >= (int)nodes.size() || maxX < minX || maxY < minY || maxCells <= 0 || minShift < 0 || minShift > 56) return;
	if (minX <= -far || minY <= -far || maxX >= far || maxY >= far) return;
	int shift = minShift;
	for (;; shift++)
	{
		const int64_t cols = ((maxX - minX) >> shift) + 1, rows = ((maxY - minY) >> shift) + 1;
		if (cols * rows <= maxCells)
		{
			out.cols = (int)cols;
			out.rows = (int)rows;
			break;
		}
	}
	out.shift = shift;
	out.originX = minX;
	out.originY = minY;
	out.start.assign((size_t)out.cols * out.rows, root);
	Fill(nodes, out, 0, 0, out.cols, out.rows, root);
}

int ComputeGridStart(const StartGrid &grid, int64_t x, int64_t y)
{
	if (grid.start.empty() || x < grid.originX || y < grid.originY) return grid.root;
	const int64_t c = (x - grid.originX) >> grid.shift, r = (y - grid.originY) >> grid.shift;
	if (c >= grid.cols || r >= grid.rows) return grid.root;
	return grid.start[(size_t)r * grid.cols + c];
}

int ComputeWalk(const std::vector<GridNode> &nodes, int from, int64_t x, int64_t y)
{
	int at = from;
	while (at >= 0) at = nodes[at].child[ComputeNodeSide(x, y, nodes[at])];
	return at;
}

int ComputeGridMismatches(const std::vector<GridNode> &nodes, const StartGrid &grid, int interior, uint64_t seed, int &tested)
{
	int mismatches = 0;
	tested = 0;
	if (grid.start.empty()) return 0;
	const int64_t size = (int64_t)1 << grid.shift;
	auto check = [&](int64_t x, int64_t y) {
		tested++;
		if (ComputeWalk(nodes, ComputeGridStart(grid, x, y), x, y) != ComputeWalk(nodes, grid.root, x, y)) mismatches++;
	};
	for (int r = 0; r < grid.rows; r++)
		for (int c = 0; c < grid.cols; c++)
		{
			const int64_t x0 = grid.originX + ((int64_t)c << grid.shift), y0 = grid.originY + ((int64_t)r << grid.shift);
			check(x0, y0);
			check(x0 + size - 1, y0);
			check(x0, y0 + size - 1);
			check(x0 + size - 1, y0 + size - 1);
			for (int i = 0; i < interior; i++)
			{
				seed = seed * 6364136223846793005ull + 1442695040888963407ull;
				check(x0 + (int64_t)((seed >> 20) % (uint64_t)size), y0 + (int64_t)((seed >> 40) % (uint64_t)size));
			}
		}
	return mismatches;
}

}} // namespace zx::bspgrid
