// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/seamless_compute.h"

namespace zx { namespace levelmesh {

int ComputeVertexHeights(const float *heights, int count, float *out)
{
	int n = 0;
	for (int h = 0; h < count; h++)
	{
		const float height = heights[h];
		int k = 0;
		while (k < n && out[k] < height) k++;
		if (k < n && out[k] == height) continue;
		for (int m = n; m > k; m--) out[m] = out[m - 1];
		out[k] = height;
		n++;
	}
	return n <= 2 ? 0 : n;
}

namespace {

// [rc4l] The fan being built, which counts past max so an overrun is seen.
struct Fan
{
	SeamVertex *out;
	int max, n;
	void Put(float x, float y, float z, float u, float v)
	{
		if (n < max) { SeamVertex &p = out[n]; p.x = x; p.y = y; p.z = z; p.u = u; p.v = v; }
		n++;
	}
	void Put(const SeamVertex &c) { Put(c.x, c.y, c.z, c.u, c.v); }
};

// [rc4l] SplitLeftEdge: the left vertex's heights strictly inside the edge, bottom up.
void SplitLeft(const SeamVertex *c, const SeamSplits &s, Fan &f)
{
	const float polyh = c[1].z - c[0].z;
	const float fu = polyh != 0.f ? (c[1].u - c[0].u) / polyh : 0.f;
	const float fv = polyh != 0.f ? (c[1].v - c[0].v) / polyh : 0.f;
	int i = 0;
	while (i < s.leftCount && s.leftHeights[i] <= c[0].z) i++;
	for (; i < s.leftCount && s.leftHeights[i] < c[1].z; i++)
	{
		const float h = s.leftHeights[i];
		f.Put(c[0].x, c[0].y, h, fu * (h - c[1].z) + c[1].u, fv * (h - c[1].z) + c[1].v);
	}
}

// [rc4l] SplitRightEdge: the right vertex's heights inside the edge, top down, never the lowest of them, as GL's loop stops above index 0.
void SplitRight(const SeamVertex *c, const SeamSplits &s, Fan &f)
{
	const float polyh = c[2].z - c[3].z;
	const float fu = polyh != 0.f ? (c[2].u - c[3].u) / polyh : 0.f;
	const float fv = polyh != 0.f ? (c[2].v - c[3].v) / polyh : 0.f;
	int i = s.rightCount - 1;
	while (i > 0 && s.rightHeights[i] >= c[2].z) i--;
	for (; i > 0 && s.rightHeights[i] > c[3].z; i--)
	{
		const float h = s.rightHeights[i];
		f.Put(c[3].x, c[3].y, h, fu * (h - c[2].z) + c[2].u, fv * (h - c[2].z) + c[2].v);
	}
}

// [rc4l] SplitUpperEdge and SplitLowerEdge: the sidedef's seg ends inside the wall, along the edge from `from` toward `to`, left to right on top and right to left below.
void SplitAlong(const SeamVertex &from, const SeamVertex &to, const SeamSplits &s, bool upper, Fan &f)
{
	const float polyw = s.fracRight - s.fracLeft;
	const float fz = (to.z - from.z) / polyw, fu = (to.u - from.u) / polyw, fv = (to.v - from.v) / polyw;
	for (int k = 0; k < s.segCount; k++)
	{
		const int i = upper ? k : s.segCount - 1 - k;
		const float frac = s.segFrac[i];
		if (upper ? frac <= s.fracLeft : frac >= s.fracRight) continue;
		if (upper ? frac >= s.fracRight : frac <= s.fracLeft) return;
		const float along = frac - s.fracLeft;
		f.Put(s.segX[i], s.segY[i], from.z + fz * along, from.u + fu * along, from.v + fv * along);
	}
}

} // namespace

bool ComputeWallTakesSeams(bool blended, bool sameSectorMiddle)
{
	return !blended && !sameSectorMiddle;
}

void ComputeBandSeams(int band, int bands, bool &upper, bool &lower)
{
	upper = band == 0;
	lower = band == bands - 1;
}

int ComputeSeamlessFan(const SeamVertex corners[4], const SeamSplits &s, SeamVertex *out, int max)
{
	Fan f = { out, max, 0 };
	f.Put(corners[0]);
	if (s.left) SplitLeft(corners, s, f);
	f.Put(corners[1]);
	if (s.upper) SplitAlong(corners[1], corners[2], s, true, f);
	f.Put(corners[2]);
	if (s.right) SplitRight(corners, s, f);
	f.Put(corners[3]);
	if (s.lower) SplitAlong(corners[0], corners[3], s, false, f);
	return f.n <= max ? f.n : 0;
}

}} // namespace zx::levelmesh
