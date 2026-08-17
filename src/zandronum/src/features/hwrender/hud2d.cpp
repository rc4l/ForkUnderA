// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/hud2d.h"

#include "tarray.h"
#include "v_video.h"

namespace zx { namespace hwrender {

static TArray<Quad2D> g_quads;
static unsigned int   g_gen = 1;
static int            g_screenW = 0, g_screenH = 0;

void Clear2D()
{
	// Clear, not free: the list settles at the frame's high-water mark and stops allocating.
	g_quads.Clear();
	g_gen++;
	g_screenW = g_screenH = 0;
}

void Record2D(const Quad2D &q)
{
	// [rc4l] A ceiling, for the same reason the sprite stream has one: an earlier version of the
	// level mesh grew without bound and took the process to 47 GB. A busy frame of Doom 2D is a few
	// thousand quads (text is one per character), so 64k is far past anything real.
	if (g_quads.Size() >= 65536) return;

	if (g_screenW == 0 && screen != NULL)
	{
		g_screenW = screen->GetWidth();
		g_screenH = screen->GetHeight();
	}
	g_quads.Push(q);
}

const Quad2D *Quads2D(int &count)
{
	count = (int)g_quads.Size();
	return count ? &g_quads[0] : NULL;
}

unsigned int Generation2D() { return g_gen; }

void GetScreen2D(int &w, int &h)
{
	w = g_screenW;
	h = g_screenH;
}


// [rc4l] The sky fade layer. Lives here rather than in the backend because the value comes from the
// GL scene layer, and this file is already the one place both sides can see.
static float g_skyFog[4] = { 0.f, 0.f, 0.f, 0.f };

void SetSkyFog(int r, int g, int b, float a)
{
	g_skyFog[0] = r / 255.f; g_skyFog[1] = g / 255.f; g_skyFog[2] = b / 255.f; g_skyFog[3] = a;
}

void GetSkyFog(float &r, float &g, float &b, float &a)
{
	r = g_skyFog[0]; g = g_skyFog[1]; b = g_skyFog[2]; a = g_skyFog[3];
}

}} // namespace zx::hwrender
