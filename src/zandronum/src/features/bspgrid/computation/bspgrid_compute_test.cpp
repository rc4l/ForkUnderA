// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/bspgrid/computation/bspgrid_compute.h"

#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <vector>

using namespace zx::bspgrid;

namespace {

const int64_t kUnit = 65536;   // [Doruk-Aksoy] one map unit in raw 48.16

// [Doruk-Aksoy] The subsector a walk from `from` ends in, as P_PointInSubsector walks.
int Walk(const std::vector<GridNode> &nodes, int from, int64_t x, int64_t y)
{
	return ComputeWalk(nodes, from, x, y);
}

struct Rng
{
	uint64_t s;
	uint64_t Next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return s >> 17; }
	int64_t In(int64_t lo, int64_t hi) { return lo + (int64_t)(Next() % (uint64_t)(hi - lo + 1)); }
};

// [Doruk-Aksoy] A random tree over the box: partitions through a point of the box, diagonal, axis-aligned or steep, leaves numbered as they come.
int Build(std::vector<GridNode> &nodes, Rng &rng, int depth, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int &leaves)
{
	if (depth == 0 || rng.Next() % 9 == 0) return -1 - leaves++;
	GridNode n;
	n.x = rng.In(x0, x1);
	n.y = rng.In(y0, y1);
	switch (rng.Next() % 4)
	{
	case 0: n.dx = rng.In(-64, 64) * kUnit; n.dy = 0; break;
	case 1: n.dx = 0; n.dy = rng.In(-64, 64) * kUnit; break;
	default: n.dx = rng.In(-4096, 4096) * kUnit + rng.In(-9, 9); n.dy = rng.In(-4096, 4096) * kUnit + rng.In(-9, 9); break;
	}
	if (n.dx == 0 && n.dy == 0) n.dx = kUnit;
	const int at = (int)nodes.size();
	nodes.push_back(n);
	const int front = Build(nodes, rng, depth - 1, x0, y0, x1, y1, leaves);
	const int back = Build(nodes, rng, depth - 1, x0, y0, x1, y1, leaves);
	nodes[at].child[0] = front;
	nodes[at].child[1] = back;
	return at;
}

// [Doruk-Aksoy] Every point given lands where the walk from the root does.
void ExpectSameLeaves(const std::vector<GridNode> &nodes, int root, const StartGrid &grid, const std::vector<int64_t> &points)
{
	for (size_t i = 0; i + 1 < points.size(); i += 2)
	{
		const int64_t x = points[i], y = points[i + 1];
		ASSERT_EQ(Walk(nodes, ComputeGridStart(grid, x, y), x, y), Walk(nodes, root, x, y)) << "at " << x << "," << y;
	}
}

} // namespace

// [Doruk-Aksoy] The side test is R_PointOnSide's: behind a partition is 1, in front or on it 0.
TEST(BspGridCompute, SideMatchesPointOnSide)
{
	const GridNode up = { 0, 0, 0, 64 * kUnit, { -1, -2 } };   // a partition along +y through the origin
	EXPECT_EQ(ComputeNodeSide(-kUnit, 5 * kUnit, up), 1);
	EXPECT_EQ(ComputeNodeSide(kUnit, 5 * kUnit, up), 0);
	EXPECT_EQ(ComputeNodeSide(0, 5 * kUnit, up), 0) << "on the line is the front";
}

// [Doruk-Aksoy] Thousands of points, at random, on the partitions, on cell edges and outside the map, land where a walk from the root does in random trees.
TEST(BspGridCompute, StartsLandWhereTheRootDoes)
{
	Rng rng = { 7 };
	for (int tree = 0; tree < 60; tree++)
	{
		std::vector<GridNode> nodes;
		int leaves = 0;
		const int64_t x0 = rng.In(-40000, 0) * kUnit, y0 = rng.In(-40000, 0) * kUnit;
		const int64_t x1 = x0 + rng.In(64, 40000) * kUnit, y1 = y0 + rng.In(64, 40000) * kUnit;
		const int root = Build(nodes, rng, 4 + tree % 10, x0, y0, x1, y1, leaves);
		if (root < 0) continue;
		StartGrid grid;
		ComputeStartGrid(nodes, root, x0, y0, x1, y1, 22, 1 << 14, grid);
		ASSERT_FALSE(grid.start.empty());
		std::vector<int64_t> points;
		for (int i = 0; i < 3000; i++) { points.push_back(rng.In(x0 - kUnit * 300, x1 + kUnit * 300)); points.push_back(rng.In(y0 - kUnit * 300, y1 + kUnit * 300)); }
		for (const GridNode &n : nodes)
			for (int k = -3; k <= 3; k++) { points.push_back(n.x + n.dx * k / 4); points.push_back(n.y + n.dy * k / 4); }
		for (int c = 0; c <= grid.cols; c += 1 + grid.cols / 40)
			for (int64_t d : { (int64_t)-1, (int64_t)0 })
			{
				points.push_back(grid.originX + ((int64_t)c << grid.shift) + d);
				points.push_back(rng.In(y0, y1));
			}
		ExpectSameLeaves(nodes, root, grid, points);
		int tested = 0;
		EXPECT_EQ(ComputeGridMismatches(nodes, grid, 3, 99, tested), 0);
		EXPECT_EQ(tested, grid.cols * grid.rows * 7);
	}
}

// [Doruk-Aksoy] The load-time check counts a cell whose start is wrong, and an empty grid tests nothing.
TEST(BspGridCompute, TheCheckFindsAWrongStart)
{
	const std::vector<GridNode> nodes = { { 32 * kUnit, 0, 0, 64 * kUnit, { -1, -2 } } };
	StartGrid grid;
	ComputeStartGrid(nodes, 0, -1024 * kUnit, -1024 * kUnit, 1024 * kUnit, 1024 * kUnit, 22, 1 << 16, grid);
	int tested = 0;
	EXPECT_EQ(ComputeGridMismatches(nodes, grid, 2, 5, tested), 0);
	for (int &s : grid.start) if (s == -2) s = -1;   // [Doruk-Aksoy] every cell behind the partition now claims the front's subsector
	EXPECT_GT(ComputeGridMismatches(nodes, grid, 2, 5, tested), 0);
	StartGrid empty;
	EXPECT_EQ(ComputeGridMismatches(nodes, empty, 2, 5, tested), 0);
	EXPECT_EQ(tested, 0);
}

// [Doruk-Aksoy] Cells that lie wholly in one subsector start at it, so most lookups walk nothing.
TEST(BspGridCompute, ACellInsideOneSubsectorStartsThere)
{
	const std::vector<GridNode> nodes = { { 32 * kUnit, 0, 0, 64 * kUnit, { -1, -2 } } };   // [Doruk-Aksoy] along +y at x 32, inside a 64-unit cell
	StartGrid grid;
	ComputeStartGrid(nodes, 0, -1024 * kUnit, -1024 * kUnit, 1024 * kUnit, 1024 * kUnit, 22, 1 << 16, grid);
	EXPECT_EQ(ComputeGridStart(grid, -500 * kUnit, 3 * kUnit), -2) << "behind the partition";
	EXPECT_EQ(ComputeGridStart(grid, 500 * kUnit, 3 * kUnit), -1) << "in front of it";
	EXPECT_EQ(ComputeGridStart(grid, 40 * kUnit, 3 * kUnit), 0) << "a cell the partition crosses starts at the node";
	EXPECT_EQ(ComputeGridStart(grid, -5000 * kUnit, 0), 0) << "outside the map, the root";
	EXPECT_EQ(ComputeGridStart(grid, 5000 * kUnit, 0), 0);
	EXPECT_EQ(ComputeGridStart(grid, 0, 5000 * kUnit), 0);
	EXPECT_EQ(ComputeGridStart(grid, 0, -5000 * kUnit), 0);
}

// [Doruk-Aksoy] Where the arithmetic could leave exact integers, the corners are not trusted and the walk starts above.
TEST(BspGridCompute, InexactNodesAreNeverSkipped)
{
	std::vector<GridNode> nodes = {
		{ 0, 0, INT32_MIN, INT32_MIN, { 1, -1 } },
		{ 0, 0, (int64_t)1 << 50, 0, { -2, -3 } },
	};
	StartGrid grid;
	ComputeStartGrid(nodes, 0, -1024 * kUnit, -1024 * kUnit, 1024 * kUnit, 1024 * kUnit, 22, 1 << 12, grid);
	for (int r = 0; r < grid.rows; r++)
		for (int c = 0; c < grid.cols; c++) EXPECT_EQ(grid.start[(size_t)r * grid.cols + c], 0);
	nodes[0].dx = 64 * kUnit;
	nodes[0].dy = 0;
	ComputeStartGrid(nodes, 0, kUnit, -1024 * kUnit, 1024 * kUnit, -kUnit, 22, 1 << 12, grid);
	EXPECT_EQ(ComputeGridStart(grid, 500 * kUnit, -500 * kUnit), 1) << "past the first node, the huge partition is not trusted";
}

// [Doruk-Aksoy] A grid too fine for its cell budget coarsens, and nothing to build makes an empty grid that starts every walk at the root.
TEST(BspGridCompute, CellsAndInputsAreBounded)
{
	const std::vector<GridNode> nodes = { { 0, 0, 0, 64 * kUnit, { -1, -2 } } };
	StartGrid grid;
	ComputeStartGrid(nodes, 0, 0, 0, 40000 * kUnit, 40000 * kUnit, 22, 100, grid);
	EXPECT_LE(grid.cols * grid.rows, 100);
	EXPECT_GT(grid.shift, 22);
	ComputeStartGrid(nodes, 0, 0, 0, 40000 * kUnit, 40000 * kUnit, 22, 1, grid);
	EXPECT_EQ(grid.cols * grid.rows, 1) << "one cell is as coarse as it goes";
	for (int64_t big : { INT64_MAX / 2, INT64_MIN / 2 })
	{
		ComputeStartGrid(nodes, 0, big < 0 ? big : 0, 0, big < 0 ? 0 : big, kUnit, 22, 100, grid);
		EXPECT_TRUE(grid.start.empty()) << "a box past half a billion map units gets no grid";
		ComputeStartGrid(nodes, 0, 0, big < 0 ? big : 0, kUnit, big < 0 ? 0 : big, 22, 100, grid);
		EXPECT_TRUE(grid.start.empty());
	}
	ComputeStartGrid(nodes, 1, 0, 0, kUnit, kUnit, 22, 100, grid);
	EXPECT_TRUE(grid.start.empty());
	EXPECT_EQ(ComputeGridStart(grid, 0, 0), 1) << "no grid starts at the root";
	ComputeStartGrid(nodes, 0, kUnit, 0, 0, kUnit, 22, 100, grid);
	EXPECT_TRUE(grid.start.empty());
	ComputeStartGrid(nodes, 0, 0, kUnit, kUnit, 0, 22, 100, grid);
	EXPECT_TRUE(grid.start.empty());
	ComputeStartGrid(nodes, 0, 0, 0, kUnit, kUnit, 22, 0, grid);
	EXPECT_TRUE(grid.start.empty());
	ComputeStartGrid(nodes, 0, 0, 0, kUnit, kUnit, -1, 100, grid);
	EXPECT_TRUE(grid.start.empty());
	ComputeStartGrid(nodes, 0, 0, 0, kUnit, kUnit, 57, 100, grid);
	EXPECT_TRUE(grid.start.empty());
	ComputeStartGrid(nodes, -1, 0, 0, kUnit, kUnit, 22, 100, grid);
	EXPECT_TRUE(grid.start.empty());
}
