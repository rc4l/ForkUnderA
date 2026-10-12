// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/hud2d.h"

#include "tarray.h"
#include "v_video.h"
#include "stats.h"
#include "i_system.h"
#include "features/hwrender/computation/rendererrate_compute.h"
#include "c_dispatch.h"
#include "gl/textures/gl_material.h"   // [rc4l] fua_2d_list names a quad's texture

namespace zx { namespace hwrender {

static TArray<Quad2D> g_quads;
static unsigned int   g_gen = 1;
static int            g_screenW = 0, g_screenH = 0;
static int            g_target = k2DBoth;

void Set2DTarget(int target) { g_target = target; }
int  Get2DTarget() { return g_target; }

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


static cycle_t       g_frameClock;
static RendererRate  g_glRate = ComputeRateStart(), g_backendRate = ComputeRateStart();

void FrameTimingBegin()
{
	g_frameClock.Reset();
	g_frameClock.Clock();
}

// [rc4l] The time since the last split, added to rate, and the clock started again for the next part of the frame.
static void SplitInto(RendererRate &rate)
{
	g_frameClock.Unclock();
	ComputeRateFrame(rate, I_MSTime(), g_frameClock.TimeMS());
	g_frameClock.Reset();
	g_frameClock.Clock();
}

void GLFrameDone() { SplitInto(g_glRate); }
void BackendFrameDone() { SplitInto(g_backendRate); }
bool BackendRateLive() { return ComputeRateLive(g_backendRate, I_MSTime()); }

void RendererRateShown(bool backend, int &ms, int &fps)
{
	const RendererRate &r = backend ? g_backendRate : g_glRate;
	ms = r.shownMs;
	fps = r.shownFps;
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
		const FMaterial *m = (const FMaterial *)q.material;
		Printf("  %u: %.0f,%.0f %.0fx%.0f %s alpha %.2f target %d\n", i, q.x, q.y, q.w, q.h, (m != NULL && m->tex != NULL) ? m->tex->Name.GetChars() : "(fill)", q.a, q.target);
		shown++;
	}
	Printf("%u 2D draws, %d of them a quarter of the screen or more\n", g_quads.Size(), shown);
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
