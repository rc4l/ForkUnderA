// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/modelreach/computation/modelreach_compute.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace zx { namespace modelreach {

namespace {

float Length3(const float v[3])
{
	return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// [Doruk-Aksoy] Model space is (x, up, y), so the flat axes are the first and the last.
float LengthFlat(const float v[3])
{
	return std::sqrt(v[0] * v[0] + v[2] * v[2]);
}

void SortUnique(std::vector<int> &v)
{
	std::sort(v.begin(), v.end());
	v.erase(std::unique(v.begin(), v.end()), v.end());
}

// [Doruk-Aksoy] The blockmap cells a span covers, clamped to the map's.
void CellSpan(double lo, double hi, double org, double blockSize, int cells, int &first, int &last)
{
	first = std::max(0, (int)std::floor((lo - org) / blockSize));
	last = std::min(cells - 1, (int)std::floor((hi - org) / blockSize));
}

} // namespace

VertexReach ComputeVertexReach(const float *xyz, int count, int stride)
{
	VertexReach r = { 0.f, 0.f };
	for (int i = 0; i < count; i++)
	{
		const float *v = xyz + (size_t)i * stride;
		r.full = std::max(r.full, Length3(v));
		r.flat = std::max(r.flat, LengthFlat(v));
	}
	return r;
}

void ComputeAddFrameReach(const VertexReach &v, const FramePlacement &p, ModelReach &into)
{
	// [Doruk-Aksoy] Stretch only ever grows up, and turns keep lengths, so the scale's largest axis and the offset bound the rest.
	const float largest = std::max(std::fabs(p.scale[0]), std::max(std::fabs(p.scale[1]), std::fabs(p.scale[2])));
	const float fullScaled = largest * std::max(1.f, std::fabs(p.stretch)) * v.full + Length3(p.offset);
	// [Doruk-Aksoy] A spin about a centre moves a point at most twice the centre's distance from the origin.
	const float fullFixed = p.rotating ? 2.f * Length3(p.spinCenter) : 0.f;
	const bool spinTilts = p.rotating && LengthFlat(p.spinAxis) > 0.f;
	float flatScaled = fullScaled, flatFixed = fullFixed;
	if (!p.tilts && !spinTilts)
	{
		// [Doruk-Aksoy] Turned only about up, the flat axes keep to themselves.
		flatScaled = std::max(std::fabs(p.scale[0]), std::fabs(p.scale[2])) * v.flat + LengthFlat(p.offset);
		flatFixed = p.rotating ? 2.f * LengthFlat(p.spinCenter) : 0.f;
	}
	into.flatScaled = std::max(into.flatScaled, flatScaled);
	into.flatFixed = std::max(into.flatFixed, flatFixed);
	into.fullScaled = std::max(into.fullScaled, fullScaled);
	into.fullFixed = std::max(into.fullFixed, fullFixed);
}

void ComputeApplyRenderRadius(double renderRadius, ModelReach &r)
{
	if (renderRadius < 0) r.off = true;
	else r.floor = std::max(r.floor, (float)renderRadius);
}

void ComputeActorReach(const ModelReach &r, float scaleX, float scaleY, float &footprint, float &full)
{
	if (r.off)
	{
		footprint = full = 0.f;
		return;
	}
	const float flatScale = std::fabs(scaleX);
	const float fullScale = std::max(flatScale, std::fabs(scaleY));
	footprint = std::max(r.floor, r.flatScaled * flatScale + r.flatFixed);
	full = std::max(r.floor, r.fullScaled * fullScale + r.fullFixed);
}

bool ComputeReachIsWide(float footprint, float radius)
{
	return footprint > radius;
}

Box ComputeFootprintBox(double prevX, double prevY, double x, double y, double reach)
{
	const Box b = { std::min(prevX, x) - reach, std::min(prevY, y) - reach, std::max(prevX, x) + reach, std::max(prevY, y) + reach };
	return b;
}

bool ComputeBoxCrossesLine(const Box &b, double x1, double y1, double x2, double y2)
{
	if (std::max(x1, x2) <= b.left || std::min(x1, x2) >= b.right || std::max(y1, y2) <= b.bottom || std::min(y1, y2) >= b.top) return false;
	const double dx = x2 - x1, dy = y2 - y1;
	const double corners[4][2] = { { b.left, b.bottom }, { b.left, b.top }, { b.right, b.bottom }, { b.right, b.top } };
	bool front = false, back = false;
	for (int i = 0; i < 4; i++)
	{
		const double side = (corners[i][0] - x1) * dy - (corners[i][1] - y1) * dx;
		front = front || side >= 0;
		back = back || side <= 0;
	}
	return front && back;
}

void ComputeBlockmapLines(const int *blockmap, const int *lump, int width, int height, double orgX, double orgY, double blockSize, const Box &b,
	std::vector<int> &out)
{
	out.clear();
	int x0, x1, y0, y1;
	CellSpan(b.left, b.right, orgX, blockSize, width, x0, x1);
	CellSpan(b.bottom, b.top, orgY, blockSize, height, y0, y1);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
			for (const int *list = lump + blockmap[y * width + x] + 1; *list != -1; list++) out.push_back(*list);
	SortUnique(out);
}

void ComputeFootprintSectors(const FootLine *lines, int count, const Box &b, int centreSector, std::vector<int> &out)
{
	out.clear();
	if (centreSector >= 0) out.push_back(centreSector);
	for (int i = 0; i < count; i++)
	{
		const FootLine &l = lines[i];
		if (!ComputeBoxCrossesLine(b, l.x1, l.y1, l.x2, l.y2)) continue;
		if (l.front >= 0) out.push_back(l.front);
		if (l.back >= 0) out.push_back(l.back);
	}
	SortUnique(out);
}

bool ComputeAnyReached(const int *sectors, int count, const unsigned *stamps, int stampCount, unsigned stamp)
{
	for (int i = 0; i < count; i++)
		if (sectors[i] >= 0 && sectors[i] < stampCount && stamps[sectors[i]] == stamp) return true;
	return false;
}

}} // namespace zx::modelreach
