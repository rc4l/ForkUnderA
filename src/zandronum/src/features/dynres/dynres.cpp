// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/dynres/dynres.h"
#include "features/dynres/computation/dynres_compute.h"
#include "features/frame-limit/framelimit.h"
#include "features/framephase/framephase.h"
#include "features/hwrender/computation/texfilter_compute.h"
#include "features/mainloop/mainloop.h"
#include "features/telemetry/telemetry.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "zstring.h"

#include <algorithm>

// [rc4l] Dynamic resolution, on by default; the scene is drawn at full scale when it is off.
CVAR(Bool, vid_fua_dynres, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// [rc4l] The frame rate to hold, 0 for the display's refresh.
CUSTOM_CVAR(Int, vid_fua_dynres_target, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0) self = 0;
	else if (self > 1000) self = 1000;
}
// [rc4l] The lowest scale the scene may be drawn at.
CUSTOM_CVAR(Float, vid_fua_dynres_min, 0.5f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < 0.25f) self = 0.25f;
	else if (self > 1.f) self = 1.f;
}
// [rc4l] Pins the scale for tests and comparisons, 0 leaving it to the controller.
CVAR(Float, fua_dg_dynres_force, 0.f, 0)

EXTERN_CVAR(Bool, vid_vsync)
EXTERN_CVAR(Int, r_texture_filter)

namespace zx { namespace dynres {

namespace {

// [rc4l] Frames a new level's uploads may take before the controller reads the GPU.
const int kLevelSettleFrames = 30;

DynresState g_state;
float g_scale = 1.f;
double g_budgetMs = 0.0;
unsigned g_level = 0;
bool g_gpuTimed = false;
const char *g_held = "";

FrameRefresh g_frames;
double g_refreshHz = 0.0;
int g_refreshAge = 0;

// [rc4l] The refresh the budget is made from: a desktop asks its display every few seconds, and a browser, which cannot ask, reads its own frames.
double RefreshHz(double frameMs)
{
	if (!zx::mainloop::Stepped())
	{
		if (g_refreshAge-- <= 0) { g_refreshHz = DisplayRefreshHz(); g_refreshAge = 600; }
		return g_refreshHz;
	}
	g_refreshHz = ComputeFrameRefreshHz(g_frames, frameMs);
	return g_refreshHz;
}

void Publish()
{
	static const zx::telemetry::Counter scale = zx::telemetry::Register(zx::telemetry::CounterSpec{ "frame/dynres_scale", "scale", zx::telemetry::CounterKind::Gauge, 0, false });
	static const zx::telemetry::Counter budget = zx::telemetry::Register(zx::telemetry::CounterSpec{ "frame/dynres_budget_ms", "ms", zx::telemetry::CounterKind::Gauge, 0, false });
	static const zx::telemetry::Counter changes = zx::telemetry::Register(zx::telemetry::CounterSpec{ "frame/dynres_changes", "changes", zx::telemetry::CounterKind::Gauge, 0, false });
	zx::telemetry::Set(scale, g_scale);
	zx::telemetry::Set(budget, g_budgetMs);
	zx::telemetry::Set(changes, g_state.changes);
}

float Hold(const char *why, float scale)
{
	g_held = why;
	g_state.step = 0;
	g_state.settle = 0;
	g_state.roomFrames = 0;
	g_state.haveAvg = false;
	g_scale = scale;
	Publish();
	return g_scale;
}

}

bool WantsGpuTime()
{
	return vid_fua_dynres && fua_dg_dynres_force <= 0.f;
}

bool Active()
{
	return vid_fua_dynres || fua_dg_dynres_force > 0.f;
}

float ScaleForFrame(double gpuMs, bool mayScale, unsigned levelGeneration)
{
	const float *phases = zx::framephase::LastFramePhases();
	DynresSample x;
	x.gpuMs = gpuMs;
	g_gpuTimed = gpuMs >= 0.0;
	x.frameMs = zx::framephase::LastFrameUs() / 1000.0;
	x.cpuMs = std::max(0.0, x.frameMs - phases[zx::framephase::kPhasePresent] / 1000.0);
	DynresCaps caps;
	caps.refreshHz = RefreshHz(x.frameMs);
	caps.vsync = vid_vsync;
	caps.limitFps = zx::framelimit::LimitFps();
	caps.targetFps = vid_fua_dynres_target;
	g_budgetMs = ComputeFrameBudgetMs(caps);

	if (fua_dg_dynres_force > 0.f) return Hold("pinned by fua_dg_dynres_force", std::min(1.f, std::max(0.25f, (float)fua_dg_dynres_force)));
	if (!vid_fua_dynres) return Hold("off", 1.f);
	if (!mayScale) return Hold("held at full scale by the frame", 1.f);
	// [rc4l] A level's first frames upload it, so the controller starts over at full scale and lets them pass before it reads the GPU.
	if (levelGeneration != g_level)
	{
		g_level = levelGeneration;
		const int changes = g_state.changes;
		g_state = DynresState();
		g_state.settle = kLevelSettleFrames;
		g_state.changes = changes;
	}
	g_held = "";
	float steps[kMaxDynresSteps];
	const int count = ComputeDynresSteps(vid_fua_dynres_min, !zx::hwrender::ComputeTextureFilter(r_texture_filter).magLinear, steps);
	g_state = ComputeDynresStep(g_state, x, g_budgetMs, steps, count, DynresTuning());
	g_scale = steps[g_state.step];
	Publish();
	return g_scale;
}

}} // namespace zx::dynres

// [rc4l] Where dynamic resolution stands: the scale, the budget and the refresh it came from, the GPU's average, and what holds it if anything does.
CCMD(fua_dynres_report)
{
	FString gpu;
	if (zx::dynres::g_gpuTimed) gpu.Format("GPU average %.2f ms", zx::dynres::g_state.gpuAvgMs);
	else gpu = "GPU not timed, judged by the frames that waited on it";
	Printf("dynamic resolution: %s, scale %.3f, budget %.2f ms (refresh %.1f Hz, frame limit %d), %s, %d changes%s%s\n",
		vid_fua_dynres ? "on" : "off", zx::dynres::g_scale, zx::dynres::g_budgetMs, zx::dynres::g_refreshHz,
		zx::framelimit::LimitFps(), gpu.GetChars(), zx::dynres::g_state.changes, *zx::dynres::g_held ? ", " : "", zx::dynres::g_held);
}
