// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A read-back's pixels: rows of 4-byte texels as the GPU laid them out, to tightly packed top-down RGB.

#ifndef ZX_READBACK_COMPUTE_H
#define ZX_READBACK_COMPUTE_H

#include <cstddef>

namespace zx {

// [rc4l] h rows of w RGBA or BGRA texels, stride bytes apart, into dst (w*h*3 bytes) as RGB; bgra comes from the texture's format, never assumed.
void ComputeRowsToRGB(const unsigned char *src, size_t stride, int w, int h, bool bgra, unsigned char *dst);

} // namespace zx

#endif // ZX_READBACK_COMPUTE_H
