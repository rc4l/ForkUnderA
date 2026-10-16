// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A point riding a line: where along the line it was when a polyobject's BSP was built, and where that is once the line's ends have moved, so the copied BSP moves with its polyobject.

#ifndef ZX_POLYBLEND_COMPUTE_H
#define ZX_POLYBLEND_COMPUTE_H

#include <cmath>

namespace zx { namespace gamecopy {

// [rc4l] How far along the line from (x1, y1) to (x2, y2) the point (px, py) lies, 0 at the first end and 1 at the second, 0 for a line of no length.
inline double ComputeAlongLine(long long x1, long long y1, long long x2, long long y2, long long px, long long py)
{
	const double dx = double(x2 - x1), dy = double(y2 - y1);
	const double len2 = dx * dx + dy * dy;
	return len2 > 0 ? (double(px - x1) * dx + double(py - y1) * dy) / len2 : 0.0;
}

// [rc4l] The point t along the line from (x1, y1) to (x2, y2), each end exactly where it is at 0 and 1.
inline void ComputeOnLine(long long x1, long long y1, long long x2, long long y2, double t, long long &x, long long &y)
{
	x = x1 + std::llround(t * double(x2 - x1));
	y = y1 + std::llround(t * double(y2 - y1));
}

}} // namespace zx::gamecopy

#endif // ZX_POLYBLEND_COMPUTE_H
