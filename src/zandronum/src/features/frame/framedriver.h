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

}} // namespace zx::frame

#endif // ZX_FRAMEDRIVER_H
