// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The engine half of the per-frame render-size reconcile, shared by GL's frame buffer and the one a launch without GL draws with.

#ifndef ZX_VIDEOSCALE_H
#define ZX_VIDEOSCALE_H

#include "features/video-scale/computation/videoscale_compute.h"

class DSimpleCanvas;

namespace zx {

// [rc4l] The render size the scale cvars ask for in a client area of clientW by clientH (wantW, wantH), and what reaching it takes from renderW by renderH and the client size last reconciled.
ScaleReconcile ReconcileRenderSize(int clientW, int clientH, int renderW, int renderH, int lastClientW, int lastClientH, int &wantW, int &wantH);

// [rc4l] Resize the screen's canvas in place, with the mode state, the status bar and the 3D view that follow it.
void ResizeScreenInPlace(DSimpleCanvas *canvas, int w, int h);

} // namespace zx

#endif // ZX_VIDEOSCALE_H
