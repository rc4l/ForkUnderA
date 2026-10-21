// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] main.fp's desaturate(), which the world shader repeats line for line so a sector's desaturation reaches Vulkan.

#ifndef ZX_DESATURATE_COMPUTE_H
#define ZX_DESATURATE_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] rgb mixed toward its grey (0.3, 0.56, 0.14 of the channels) by factor, in place; a factor of 0 or less leaves it alone.
void ComputeDesaturate(float rgb[3], float factor);

// [rc4l] A colormap's 0..255 desaturation as the factor the shader mixes by.
float ComputeDesaturationFactor(int amount);

}} // namespace zx::hwrender

#endif
