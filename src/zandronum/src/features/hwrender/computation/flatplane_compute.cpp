// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/flatplane_compute.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace zx { namespace hwrender {

bool ComputeIsFlatPlane(bool flatSurface, bool solid, int blend, bool hud, float lo, float hi)
{
	return flatSurface && solid && blend != 0 && !hud && hi >= lo && hi - lo <= kFlatSliver;
}

bool ComputeFootprintsOverlap(const FlatFootprint &a, const FlatFootprint &b)
{
	return a.minX <= b.maxX && b.minX <= a.maxX && a.minY <= b.maxY && b.minY <= a.maxY;
}

int ComputeFlatSide(float lo, float hi, float h, bool eyeAbove)
{
	const float eyePart = eyeAbove ? hi - h : h - lo, beyondPart = eyeAbove ? h - lo : hi - h;
	if (eyePart <= kFlatPlaneThickness && beyondPart <= kFlatPlaneThickness) return 0;
	if (beyondPart <= kFlatSliver && eyePart >= beyondPart) return 1;
	if (eyePart <= kFlatSliver && beyondPart > eyePart) return -1;
	return 0;
}

bool ComputeStraddlesHeight(float lo, float hi, float h)
{
	return lo < h - kFlatSliver && hi > h + kFlatSliver;
}

bool FlatGrid::Cells(const FlatFootprint &a, long long &x0, long long &x1, long long &y0, long long &y1) const
{
	x0 = (long long)std::floor(a.minX / cell_); x1 = (long long)std::floor(a.maxX / cell_);
	y0 = (long long)std::floor(a.minY / cell_); y1 = (long long)std::floor(a.maxY / cell_);
	return (x1 - x0 + 1) * (y1 - y0 + 1) <= kMaxCells;
}

size_t FlatGrid::Bucket(long long x, long long y) const
{
	// [Doruk-Aksoy] Two large odd multipliers spread neighbouring cells across the buckets; a collision only costs a footprint test.
	const uint64_t h = (uint64_t)x * 0x9E3779B97F4A7C15ull ^ (uint64_t)y * 0xC2B2AE3D27D4EB4Full;
	return (size_t)(h >> 32) & mask_;
}

void FlatGrid::Reset(size_t total)
{
	size_t buckets = 16;
	while (buckets < total * 2) buckets <<= 1;
	mask_ = buckets - 1;
	start_.assign(buckets + 1, 0);
	items_.resize(total);
}

void FlatGrid::Prefix()
{
	for (size_t b = 1; b < start_.size(); b++) start_[b] += start_[b - 1];
	fill_.assign(start_.begin(), start_.end() - 1);
}

bool ComputeAnyBetween(const std::vector<float> &sorted, float lo, float hi)
{
	auto it = std::upper_bound(sorted.begin(), sorted.end(), lo);
	return it != sorted.end() && *it < hi;
}

static bool Crosses(const FlatSplitVertex &a, const FlatSplitVertex &b, float h)
{
	return (a.h - h) * (b.h - h) < 0.f;
}

static FlatSplitVertex At(const FlatSplitVertex &a, const FlatSplitVertex &b, float h)
{
	const float t = (h - a.h) / (b.h - a.h);
	return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, h, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t };
}

bool ComputeSplitQuadAtHeight(const FlatSplitVertex in[4], float h, FlatSplitVertex half01[4], FlatSplitVertex half23[4])
{
	if (!Crosses(in[0], in[2], h) || !Crosses(in[1], in[3], h)) return false;
	const FlatSplitVertex p02 = At(in[0], in[2], h), p13 = At(in[1], in[3], h);
	half01[0] = in[0]; half01[1] = in[1]; half01[2] = p02; half01[3] = p13;
	half23[0] = p02; half23[1] = p13; half23[2] = in[2]; half23[3] = in[3];
	return true;
}

float ComputeJustBehind(float d)
{
	return d + std::max(d * 1e-5f, 1e-2f);
}

void ComputeFlatPlaneOrder(FlatOrderItem *items, int n, float viewZ, FlatOrderScratch &scratch, float cellSize)
{
	std::vector<int> &planes = scratch.planes;
	planes.clear();
	for (int i = 0; i < n; i++) if (items[i].plane) planes.push_back(i);
	if (planes.empty()) return;
	auto mid = [&](int i) { return (items[i].lo + items[i].hi) * 0.5f; };
	// [Doruk-Aksoy] Nearest the eye in height first, so a draw between two planes is placed by the nearer one before the farther one places itself.
	std::sort(planes.begin(), planes.end(), [&](int a, int b) {
		const float da = std::fabs(viewZ - mid(a)), db = std::fabs(viewZ - mid(b));
		return da != db ? da < db : a < b;
	});
	std::vector<FlatFootprint> &areas = scratch.areas;
	areas.resize(n);
	for (int i = 0; i < n; i++) areas[i] = items[i].area;
	FlatGrid &grid = scratch.grid;
	grid.Build(areas.data(), n, cellSize, [&](int i) { return !items[i].plane; });

	std::vector<int> &near = scratch.near;
	for (int p : planes)
	{
		near.clear();
		grid.Query(items[p].area, [&](int c) { near.push_back(c); });
		const float h = mid(p);
		const bool above = viewZ >= h;
		// [Doruk-Aksoy] Behind everything over it on the eye's side, as a floor is drawn before what stands on it.
		for (int c : near)
			if (ComputeFlatSide(items[c].lo, items[c].hi, h, above) > 0) items[p].dist = std::max(items[p].dist, ComputeJustBehind(items[c].dist));
		// [Doruk-Aksoy] And what lies wholly beyond it behind it, as what is under a floor shows through it.
		for (int c : near)
			if (ComputeFlatSide(items[c].lo, items[c].hi, h, above) < 0) items[c].dist = std::max(items[c].dist, ComputeJustBehind(items[p].dist));
	}
}

}} // namespace zx::hwrender
