// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Dynamic resolution: the 3D scene drawn below full scale while the GPU cannot finish a frame inside its budget, and back at full scale once it can.

#ifndef ZX_DYNRES_H
#define ZX_DYNRES_H

namespace zx { namespace dynres {

// [rc4l] The scene's scale for the frame about to be drawn, from the last GPU frame time measured in ms (below 0 when unknown); mayScale false holds full scale, and a new levelGeneration starts the controller over.
float ScaleForFrame(double gpuMs, bool mayScale, unsigned levelGeneration);

// [rc4l] Whether the controller needs the GPU's frames timed, so the backend times them with telemetry off.
bool WantsGpuTime();

// [rc4l] Whether the scene may be drawn below full scale at all, on or pinned, so the backend makes what an upscale needs only then.
bool Active();

// [rc4l] The display's refresh in Hz, 0 when the platform cannot say (displayrefresh.cpp, displayrefresh_cocoa.mm).
double DisplayRefreshHz();

}} // namespace zx::dynres

#endif // ZX_DYNRES_H
