// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/wideline_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

void ComputeWideLineVertex(int k, float &at, float &side)
{
	static const float kAt[6] = { 0.f, 0.f, 1.f, 1.f, 0.f, 1.f };
	static const float kSide[6] = { 1.f, -1.f, 1.f, 1.f, -1.f, -1.f };
	at = kAt[k];
	side = kSide[k];
}

// [rc4l] p moved along its line toward q until its w is the near w.
static void PullToNear(float p[4], const float q[4])
{
	const float t = (kWideLineNearW - p[3]) / (q[3] - p[3]);
	for (int i = 0; i < 4; i++) p[i] += (q[i] - p[i]) * t;
}

bool ComputeWideLineCorner(const float a[4], const float b[4], float at, float side, float widthPx, float screenW, float screenH, float out[4])
{
	if (a[3] < kWideLineNearW && b[3] < kWideLineNearW) return false;
	float s[4], e[4];
	for (int i = 0; i < 4; i++) { s[i] = a[i]; e[i] = b[i]; }
	if (s[3] < kWideLineNearW) PullToNear(s, e);
	if (e[3] < kWideLineNearW) PullToNear(e, s);
	const float halfW = screenW * 0.5f, halfH = screenH * 0.5f;
	const float dx = (e[0] / e[3] - s[0] / s[3]) * halfW, dy = (e[1] / e[3] - s[1] / s[3]) * halfH;
	const float len = sqrtf(dx * dx + dy * dy);
	const float nx = len > 1e-6f ? -dy / len : 0.f, ny = len > 1e-6f ? dx / len : 1.f;
	const float *p = at > 0.5f ? e : s;
	for (int i = 0; i < 4; i++) out[i] = p[i];
	out[0] += nx * side * widthPx * 0.5f / halfW * p[3];
	out[1] += ny * side * widthPx * 0.5f / halfH * p[3];
	return true;
}

}} // namespace zx::hwrender
