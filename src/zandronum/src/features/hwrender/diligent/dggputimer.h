// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GPU time of the backend's passes as features/telemetry gauges in ms (gpu/frame_ms, gpu/world_ms, gpu/2d_ms), from duration queries read back frames later (dggputimer.cpp); unavailable on a device without them, such as a browser's.

#ifndef ZX_DGGPUTIMER_H
#define ZX_DGGPUTIMER_H

#include "DeviceContext.h"

namespace zx { namespace hwrender {

// [Doruk-Aksoy] kGpuPostScene and kGpuPostScreen are features/postprocess's two stages.
enum GpuPass { kGpuFrame, kGpuWorld, kGpu2D, kGpuPostScene, kGpuPostScreen, kGpuPasses };

void BeginGpuPass(Diligent::IDeviceContext *ctx, GpuPass pass);
void EndGpuPass(Diligent::IDeviceContext *ctx, GpuPass pass);
// [rc4l] Reads every pass's finished queries, the newest becoming its gauge; once a frame.
void CollectGpuPasses();
// [rc4l] Drops the queries before the device goes.
void ReleaseGpuPasses();
// [rc4l] The newest finished time of the pass in ms, below 0 until one has finished or where the device has no duration queries.
double LastGpuPassMs(GpuPass pass);

}} // namespace zx::hwrender

#endif // ZX_DGGPUTIMER_H
