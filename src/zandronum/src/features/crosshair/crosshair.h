// [MobiusU9B] Crosshair Options: the size and offset sliders layered on the stock crosshair draw.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_CROSSHAIR_H
#define ZX_CROSSHAIR_H

#include "m_fixed.h"
#include "features/crosshair/computation/crosshair_compute.h"

class FTexture;

namespace zx { namespace crosshair {

// [MobiusU9B] Where image is drawn this frame in real pixels, from the stock cvars, the sliders, growSize (CrosshairSize) and the 3D view.
CrosshairRect GetRect(FTexture *image, fixed_t growSize);

} } // namespace zx::crosshair

#endif
