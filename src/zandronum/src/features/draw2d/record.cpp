// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/record.h"

#include "tarray.h"
#include "v_video.h"
#include "c_dispatch.h"
#include "features/materials/materials.h"   // [rc4l] fua_2d_list names a quad's texture

namespace zx { namespace draw2d {


static TArray<Quad2D> g_quads;
static unsigned int   g_gen = 1;
static int            g_screenW = 0, g_screenH = 0;
static int            g_target = k2DBoth;
static std::vector<Quad2D> *g_capture = NULL;   // [rc4l] where quads go while a capture runs

void Set2DTarget(int target) { g_target = target; }
int  Get2DTarget() { return g_target; }

void Clear2D()
{
	// Clear, not free: the list settles at the frame's high-water mark and stops allocating.
	g_quads.Clear();
	g_gen++;
	g_screenW = g_screenH = 0;
}

void BeginCapture2D(std::vector<Quad2D> &into)
{
	into.clear();
	g_capture = &into;
}

void EndCapture2D()
{
	g_capture = NULL;
}

void Replay2D(const std::vector<Quad2D> &quads)
{
	const int target = g_target;
	for (const Quad2D &q : quads)
	{
		g_target = q.target;
		Record2D(q);
	}
	g_target = target;
}

void Record2D(const Quad2D &q)
{
	if (g_capture != NULL)
	{
		if (g_capture->size() < 65536) { g_capture->push_back(q); g_capture->back().target = g_target; }
		return;
	}
	// [rc4l] A ceiling far past a busy frame's few thousand quads, as the sprite stream has one since an unbounded level mesh once took the process to 47 GB.
	if (g_quads.Size() >= 65536) return;

	if (g_screenW == 0 && screen != NULL)
	{
		g_screenW = screen->GetWidth();
		g_screenH = screen->GetHeight();
	}
	g_quads.Push(q);
	g_quads.Last().target = g_target;
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


// [rc4l] fua_2d_list -- the last frame's 2D draws that cover at least a quarter of the screen, with their texture and window, for a picture something is drawn over.
CCMD(fua_2d_list)
{
	const float screenArea = (float)(g_screenW > 0 ? g_screenW : 1) * (g_screenH > 0 ? g_screenH : 1);
	int shown = 0;
	for (unsigned i = 0; i < g_quads.Size(); i++)
	{
		const Quad2D &q = g_quads[i];
		if (q.w * q.h < screenArea * 0.25f) continue;
		Printf("  %u: %.0f,%.0f %.0fx%.0f %s alpha %.2f target %d\n", i, q.x, q.y, q.w, q.h, q.material != NULL ? zx::materials::MaterialName(q.material) : "(fill)", q.a, q.target);
		shown++;
	}
	Printf("%u 2D draws, %d of them a quarter of the screen or more\n", g_quads.Size(), shown);
}

}} // namespace zx::draw2d
