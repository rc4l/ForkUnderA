// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/linemirror_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

bool ComputeMirrorEye(float nx, float ny, float d, float px, float py, unsigned angle, float &rx, float &ry, unsigned &outAngle)
{
	const float dist = nx * px + ny * py - d;
	if (dist <= 0.f) return false;
	rx = px - 2.f * dist * nx;
	ry = py - 2.f * dist * ny;
	// [rc4l] The line's own direction is (-ny, nx), the normal being (dy, -dx) over the length.
	const float twoPi = 2.f * 3.14159265f;
	const float mirrorAng = atan2f(nx, -ny);
	const float camAng = (float)(angle >> 19) * twoPi / 8192.f;
	// [rc4l] Wrapped before converting, since a negative angle into an unsigned BAM is undefined.
	float refAng = fmodf(2.f * mirrorAng - camAng, twoPi);
	if (refAng < 0.f) refAng += twoPi;
	outAngle = (unsigned)(refAng / twoPi * 4294967296.0);
	return true;
}

bool ComputeMirrorLevelDrawn(int level, int recursions)
{
	return level <= recursions;
}

}} // namespace zx::hwrender
