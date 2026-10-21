// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] ANIMDEFS warp and warp2 textures as GL draws them: a time-driven ripple of the texture coordinate, from func_warp1.fp and func_warp2.fp.

#ifndef ZX_WARP_COMPUTE_H
#define ZX_WARP_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The warp a piece's shader applies: a texture's bWarped (1 warp, 2 warp2), anything else none.
int ComputeWarpStyle(int bWarped);

// [rc4l] GL's warp clock, gl_frameMS * speed / 1000: the "timer" its warp shaders read.
float ComputeWarpTimer(unsigned int frameMS, float speed);

// [rc4l] The offset a warp adds to a texture coordinate at a timer value; fuaWarp in dgscene's shader mirrors it line for line.
void ComputeWarpOffset(int style, float u, float v, float timer, float *du, float *dv);

}} // namespace zx::hwrender

#endif // ZX_WARP_COMPUTE_H
