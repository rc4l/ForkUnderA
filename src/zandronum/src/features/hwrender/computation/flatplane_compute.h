// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] A flat blended model surface (a ground disc, a ring) ordered as a floor or ceiling would be against the sprites over its footprint.

#ifndef ZX_FLATPLANE_COMPUTE_H
#define ZX_FLATPLANE_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] How far apart two heights may be and still be level with each other.
constexpr float kFlatPlaneThickness = 0.5f;
// [Doruk-Aksoy] How far a sprite may reach past a plane and still count as standing on it, unsplit, as a corpse sunk into the floor under a blood pool does.
constexpr float kFlatSliver = 12.f;

// [Doruk-Aksoy] Whether a dynamic piece is a flat plane: a blended model surface (not the HUD's) FUAMODEL names a FlatSurface, no more than kFlatSliver thick.
bool ComputeIsFlatPlane(bool flatSurface, bool solid, int blend, bool hud, float lo, float hi);

// [Doruk-Aksoy] A rectangle on the map, x and y.
struct FlatFootprint
{
	float minX, maxX, minY, maxY;
};

// [Doruk-Aksoy] Whether two footprints overlap, edges touching included.
bool ComputeFootprintsOverlap(const FlatFootprint &a, const FlatFootprint &b);

// [Doruk-Aksoy] Which side of a plane at height h a piece spanning lo..hi is on: 1 the eye's, -1 beyond it, 0 level with it or across it; a part of kFlatSliver or less on the other side does not count against it.
int ComputeFlatSide(float lo, float hi, float h, bool eyeAbove);

// [Doruk-Aksoy] Whether a piece spanning lo..hi reaches more than kFlatSliver past the height h on both sides, and so is split at it.
bool ComputeStraddlesHeight(float lo, float hi, float h);

// [Doruk-Aksoy] Footprints filed in square cells, so a query visits only those near it.
class FlatGrid
{
public:
	// [Doruk-Aksoy] Files the footprints (areas[i] for each i where take(i)), one covering more than kMaxCells cells kept apart and visited by every query.
	template <typename Take>
	void Build(const FlatFootprint *areas, int n, float cellSize, Take take)
	{
		cell_ = cellSize; areas_ = areas; filed_.clear(); wide_.clear(); stamp_.assign(n, -1); query_ = 0;
		size_t total = 0;
		for (int i = 0; i < n; i++)
		{
			if (!take(i)) continue;
			long long x0, x1, y0, y1;
			if (!Cells(areas[i], x0, x1, y0, y1)) { wide_.push_back(i); continue; }
			filed_.push_back(i);
			total += (size_t)((x1 - x0 + 1) * (y1 - y0 + 1));
		}
		// [Doruk-Aksoy] Counted into buckets in two passes rather than sorted, so a build costs a pass over the cells.
		Reset(total);
		for (int i : filed_) ForCells(areas[i], [&](size_t b) { start_[b + 1]++; });
		Prefix();
		for (int i : filed_) ForCells(areas[i], [&](size_t b) { items_[fill_[b]++] = i; });
	}

	// [Doruk-Aksoy] Calls f once for each filed footprint overlapping `area`.
	template <typename F>
	void Query(const FlatFootprint &area, F f)
	{
		query_++;
		auto visit = [&](int i) {
			if (stamp_[i] == query_) return;
			stamp_[i] = query_;
			if (ComputeFootprintsOverlap(areas_[i], area)) f(i);
		};
		long long x0, x1, y0, y1;
		if (!Cells(area, x0, x1, y0, y1)) { for (int i : filed_) visit(i); }
		else ForCells(area, [&](size_t b) { for (unsigned k = start_[b]; k < start_[b + 1]; k++) visit(items_[k]); });
		for (int i : wide_) visit(i);
	}

	static constexpr long long kMaxCells = 256;

private:
	bool Cells(const FlatFootprint &a, long long &x0, long long &x1, long long &y0, long long &y1) const;
	size_t Bucket(long long x, long long y) const;
	void Reset(size_t total);
	void Prefix();

	template <typename F>
	void ForCells(const FlatFootprint &a, F f) const
	{
		long long x0, x1, y0, y1;
		Cells(a, x0, x1, y0, y1);
		for (long long x = x0; x <= x1; x++)
			for (long long y = y0; y <= y1; y++) f(Bucket(x, y));
	}

	float cell_ = 128.f;
	const FlatFootprint *areas_ = nullptr;
	size_t mask_ = 0;
	std::vector<unsigned> start_, fill_;
	std::vector<int> items_, filed_, wide_, stamp_;
	int query_ = 0;
};

// [Doruk-Aksoy] Whether a sorted list holds a value strictly between lo and hi.
bool ComputeAnyBetween(const std::vector<float> &sorted, float lo, float hi);

// [Doruk-Aksoy] One corner of a sprite quad, h its height.
struct FlatSplitVertex
{
	float x, y, h, u, v;
};

// [Doruk-Aksoy] A quad in strip order (0 and 1 one edge, 2 and 3 the other) cut at height h along its edges 0-2 and 1-3 into the half keeping 0 and 1 and the half keeping 2 and 3, both in strip order; false, leaving both alone, when either edge does not cross h.
bool ComputeSplitQuadAtHeight(const FlatSplitVertex in[4], float h, FlatSplitVertex half01[4], FlatSplitVertex half23[4]);

// [Doruk-Aksoy] One draw in the blend list: its squared distance (adjusted in place), height span, footprint, and whether it is a flat plane.
struct FlatOrderItem
{
	float dist;
	float lo, hi;
	FlatFootprint area;
	bool plane;
};

// [Doruk-Aksoy] The buffers ComputeFlatPlaneOrder works in, kept between calls so a frame allocates nothing.
struct FlatOrderScratch
{
	std::vector<int> planes, near;
	std::vector<FlatFootprint> areas;
	FlatGrid grid;
};

// [Doruk-Aksoy] Moves each plane behind the draws over its footprint on the eye's side of it, and what lies wholly beyond it behind the plane, by raising their distances; planes nearest the eye in height go first, and planes never move each other.
void ComputeFlatPlaneOrder(FlatOrderItem *items, int n, float viewZ, FlatOrderScratch &scratch, float cellSize = 128.f);

// [Doruk-Aksoy] A distance a hair farther than d, which a back-to-front sort draws before d.
float ComputeJustBehind(float d);

}} // namespace zx::hwrender

#endif // ZX_FLATPLANE_COMPUTE_H
