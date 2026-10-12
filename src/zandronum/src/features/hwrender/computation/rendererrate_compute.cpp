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

}} // namespace zx::hwrender
