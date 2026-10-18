// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The engine's renderer: the level half once, each frame set up once, then each renderer's view when its kind draws, so no renderer drives another.

#ifndef ZX_FRAMEDRIVER_H
#define ZX_FRAMEDRIVER_H

#include <vector>

#include "features/frame/computation/frameroute_compute.h"   // [rc4l] RendererKind

struct FRenderer;   // [rc4l] a struct, as r_renderer.h defines it, or MSVC links a different name

namespace zx { namespace frame {

// [rc4l] A renderer the driver holds, and its kind (a RendererKind), which decides on which frames it draws.
struct DrivenRenderer
{
	FRenderer *renderer;
	int        kind;
};

// [rc4l] The driver over `renderers`, any number of each kind, called in this order; it owns them.
FRenderer *CreateFrameDriver(const std::vector<DrivenRenderer> &renderers);

// [rc4l] Whether GL draws this frame in the launch's driver, false before it is made.
bool GLDrawsFrame();

// [rc4l] The renderer the backend draws the world with, in a launch without GL or beside it (backendrenderer.cpp).
FRenderer *CreateBackendRenderer();

// [rc4l] The renderer a launch draws with, which every platform's I_CreateRenderer asks for: GL's (from createGL, NULL in a build without GL) and the backend's under the frame driver, or the backend's alone in a launch without GL.
FRenderer *CreateLaunchRenderer(FRenderer *(*createGL)());

}} // namespace zx::frame

#endif // ZX_FRAMEDRIVER_H
