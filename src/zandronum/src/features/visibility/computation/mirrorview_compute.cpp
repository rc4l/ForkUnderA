// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/mirrorview_compute.h"

#include <math.h>
#include <string.h>

namespace zx { namespace visibility {

// [rc4l] FLOAT2FIXED: xs_Fix<16>::ToFix's magic add, the low word taken as a 32-bit int.
static long long ToFix16(double v)
{
	const double magic = v + 6755399441055744.0 / 65536.0;
	unsigned long long bits;
	memcpy(&bits, &magic, sizeof(bits));
	return (long long)(int)(unsigned)bits;
}

void ComputeMirrorEye(long long v1x, long long v1y, long long v2x, long long v2y, long long eyeX, long long eyeY, int depth,
	long long &outX, long long &outY)
{
	const long long dx = v2x - v1x, dy = v2y - v1y;
	outX = eyeX;
	outY = eyeY;
	if (dx == 0)
	{
		outX = v1x - eyeX + v1x;
		outX += eyeX < v1x ? -32768 : 32768;
		return;
	}
	if (dy == 0)
	{
		outY = v1y - eyeY + v1y;
		outY += eyeY < v1y ? -32768 : 32768;
		return;
	}
	// [rc4l] Doubles, as GL keeps them to avoid seams, then FVector2's float normal for the nudge.
	const double fdx = dx / 65536.0, fdy = dy / 65536.0, x1 = v1x / 65536.0, y1 = v1y / 65536.0;
	const double x = eyeX / 65536.0, y = eyeY / 65536.0;
	const double r = ((x - x1) * fdx + (y - y1) * fdy) / (fdx * fdx + fdy * fdy);
	outX = ToFix16((x1 + r * fdx) * 2 - x);
	outY = ToFix16((y1 + r * fdy) * 2 - y);
	float nx = float(-fdx), ny = float(fdy);
	double len = sqrtf(nx * nx + ny * ny);
	if (len != 0) len = 1 / len;
	nx = float(nx * len);
	ny = float(ny * len);
	outX += ToFix16(ny * depth / 2);
	outY += ToFix16(nx * depth / 2);
}

static float PlaneZ(const SheenPlane &p, float x, float y)
{
	return float((p.d + p.a * x + p.b * y) * p.ic / (-65536.0 * 65536.0));
}

// [rc4l] The wall's two pieces either side of the fraction `coeff` along it, as SplitWall cuts copyWall1 and copyWall2.
static void Cut(const SheenWall &w, float coeff, SheenWall &left, SheenWall &right)
{
	left = right = w;
	left.x2 = right.x1 = w.x1 + coeff * (w.x2 - w.x1);
	left.y2 = right.y1 = w.y1 + coeff * (w.y2 - w.y1);
	left.ztop[1] = right.ztop[0] = w.ztop[0] + coeff * (w.ztop[1] - w.ztop[0]);
	left.zbottom[1] = right.zbottom[0] = w.zbottom[0] + coeff * (w.zbottom[1] - w.zbottom[0]);
}

// [rc4l] Where the band boundary crosses edge z of the wall: snaps an end within a tenth of a unit, as GL does, or says where to cut.
static bool Crossing(const float z[2], float clen, float &left, float &right, float &cut)
{
	if (!((left < z[0] && right > z[1]) || (left > z[0] && right < z[1]))) return false;
	const float dch = z[1] - z[0], dfh = right - left;
	const float coeff = (z[0] - left) / (dfh - dch);
	if (coeff * clen < .1f) left = z[0];
	else if (coeff * clen > clen - .1f) right = z[1];
	else
	{
		cut = coeff;
		return true;
	}
	return false;
}

int ComputeSheenBand(const SheenWall &w, const SheenPlane *planes, int bands)
{
	if (w.x1 == w.x2 && w.y1 == w.y2) return kSheenNothing;
	for (int i = 0; i + 1 < bands; i++)
	{
		const SheenPlane &p = planes[i];
		float left = PlaneZ(p, w.x2, w.y2), right = left;
		if (p.sloped) left = PlaneZ(p, w.x1, w.y1);
		if (left >= w.ztop[0] && right >= w.ztop[1]) continue;
		const float clen = fabsf(w.x2 - w.x1);   // [rc4l] GL's MAX with fabsf(y2 - y2), which is always 0
		float cut = 0.f;
		if (Crossing(w.ztop, clen, left, right, cut) || Crossing(w.zbottom, clen, left, right, cut))
		{
			SheenWall a, b;
			Cut(w, cut, a, b);
			const int first = ComputeSheenBand(a, planes, bands);
			return first != kSheenNothing ? first : ComputeSheenBand(b, planes, bands);
		}
		if (left <= w.zbottom[0] && right <= w.zbottom[1]) return i > 0 ? i : kSheenOwnLight;
		if (left <= w.ztop[0] && right <= w.ztop[1] && (left != w.ztop[0] || right != w.ztop[1])) return i;
		if (w.ztop[0] == w.zbottom[0] && w.ztop[1] == w.zbottom[1]) return kSheenNothing;
	}
	return bands - 1;
}

}} // namespace zx::visibility
