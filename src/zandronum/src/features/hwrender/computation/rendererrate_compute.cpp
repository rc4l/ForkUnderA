// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/rendererrate_compute.h"

#include <stdio.h>

namespace zx { namespace hwrender {

RendererRate ComputeRateStart()
{
	RendererRate r;
	r.second = 0; r.frames = 0; r.busyMs = 0.0; r.shownMs = 0; r.shownFps = 0; r.lastMs = 0; r.seen = false;
	return r;
}

void ComputeRateFrame(RendererRate &r, unsigned nowMs, double frameMs)
{
	const unsigned second = nowMs / 1000;
	if (second != r.second)
	{
		if (r.frames > 0)
		{
			r.shownMs = (int)(r.busyMs / r.frames + 0.5);
			r.shownFps = r.busyMs > 0.0 ? (int)(r.frames * 1000.0 / r.busyMs + 0.5) : 0;
		}
		r.second = second;
		r.frames = 0;
		r.busyMs = 0.0;
	}
	r.frames++;
	r.busyMs += frameMs > 0.0 ? frameMs : 0.0;
	r.lastMs = nowMs;
	r.seen = true;
}

bool ComputeRateLive(const RendererRate &r, unsigned nowMs)
{
	return r.seen && nowMs - r.lastMs < 1000;
}

int ComputeRateText(char *out, size_t size, const char *name, int ms, int fps)
{
	if (out == NULL || size == 0) return 0;
	const int n = snprintf(out, size, "%s %2d ms (%3d fps)", name, ms, fps);
	return (size_t)n >= size ? (int)size - 1 : n;
}

RateBox ComputeRateBox(int screenWidth, int screenHeight, int chars, int scale)
{
	RateBox b;
	b.scale = scale < 1 ? 1 : scale;
	while (b.scale > 1 && (chars * 8 * b.scale > screenWidth || 8 * b.scale > screenHeight)) b.scale--;
	b.virtualWidth = screenWidth / b.scale;
	b.virtualHeight = screenHeight / b.scale;
	b.x = b.virtualWidth - chars * 8;
	// [rc4l] Mapped as the draw maps the text, so box and glyphs line up when the screen is not a multiple of the scale.
	b.boxLeft = (int)((long long)b.x * screenWidth / b.virtualWidth);
	b.boxHeight = (int)((8LL * screenHeight + b.virtualHeight - 1) / b.virtualHeight);
	return b;
}

}} // namespace zx::hwrender
