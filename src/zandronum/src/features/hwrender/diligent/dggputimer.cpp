// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See dggputimer.h; the ring's bookkeeping is features/hwrender/computation/queryring_compute.h, and this file only makes, issues and reads the queries.

#include "features/hwrender/diligent/dggputimer.h"
#include "features/hwrender/diligent/dgshared.h"   // [rc4l] GetDevice
#include "features/hwrender/computation/queryring_compute.h"
#include "features/telemetry/telemetry.h"
#include "features/dynres/dynres.h"   // [rc4l] WantsGpuTime

#include "RenderDevice.h"
#include "Query.h"
#include "RefCntAutoPtr.hpp"

namespace zx { namespace hwrender {

namespace {

struct PassTimer
{
	const char *name;
	zx::telemetry::Counter counter = -1;
	QueryRing ring;
	Diligent::RefCntAutoPtr<Diligent::IQuery> queries[QueryRing::kSlots];
	int open = -1;
	bool unsupported = false;
	double lastMs = -1.0;
};

PassTimer g_passes[kGpuPasses] = { { "gpu/frame_ms" }, { "gpu/world_ms" }, { "gpu/2d_ms" }, { "gpu/post_scene_ms" }, { "gpu/post_screen_ms" } };

// [rc4l] The pass's queries, made the first time it is timed; false, its gauge marked unavailable, where the device has no duration queries.
bool Ready(PassTimer &t)
{
	if (t.counter < 0) t.counter = zx::telemetry::Register(zx::telemetry::CounterSpec{ t.name, "ms", zx::telemetry::CounterKind::Gauge, 0 });
	if (t.unsupported) return false;
	if (t.queries[0]) return true;
	Diligent::IRenderDevice *device = GetDevice();
	if (device == nullptr || device->GetDeviceInfo().Features.DurationQueries == Diligent::DEVICE_FEATURE_STATE_DISABLED)
	{
		t.unsupported = true;
		zx::telemetry::Set(t.counter, -1.0);
		return false;
	}
	Diligent::QueryDesc qd;
	qd.Name = t.name;
	qd.Type = Diligent::QUERY_TYPE_DURATION;
	for (int i = 0; i < QueryRing::kSlots; i++) device->CreateQuery(qd, &t.queries[i]);
	return true;
}

} // namespace

void BeginGpuPass(Diligent::IDeviceContext *ctx, GpuPass pass)
{
	PassTimer &t = g_passes[pass];
	// [rc4l] features/dynres reads the frame's time with telemetry off too.
	const bool wanted = zx::telemetry::g_enabled || (pass == kGpuFrame && zx::dynres::WantsGpuTime());
	if (!wanted || t.open >= 0 || !Ready(t)) return;
	t.open = t.ring.Begin();
	if (t.open >= 0 && t.queries[t.open]) ctx->BeginQuery(t.queries[t.open]);
}

void EndGpuPass(Diligent::IDeviceContext *ctx, GpuPass pass)
{
	PassTimer &t = g_passes[pass];
	if (t.open < 0) return;
	if (t.queries[t.open]) ctx->EndQuery(t.queries[t.open]);
	t.open = -1;
}

void CollectGpuPasses()
{
	for (PassTimer &t : g_passes)
	{
		t.ring.Collect([&](int slot)
		{
			Diligent::QueryDataDuration d;
			if (!t.queries[slot]) return true;
			if (!t.queries[slot]->GetData(&d, sizeof(d))) return false;
			t.lastMs = ComputeQueryMs(d.Duration, d.Frequency);
			zx::telemetry::Set(t.counter, t.lastMs);
			return true;
		});
	}
}

void ReleaseGpuPasses()
{
	for (PassTimer &t : g_passes)
	{
		for (Diligent::RefCntAutoPtr<Diligent::IQuery> &q : t.queries) q.Release();
		t.ring = QueryRing();
		t.open = -1;
		t.lastMs = -1.0;
	}
}

double LastGpuPassMs(GpuPass pass)
{
	return g_passes[pass].lastMs;
}

}} // namespace zx::hwrender
