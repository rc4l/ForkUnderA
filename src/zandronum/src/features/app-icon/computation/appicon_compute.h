// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The application icon brought down to the size a window system shows it at.

#ifndef ZX_APPICON_COMPUTE_H
#define ZX_APPICON_COMPUTE_H

namespace zx { namespace appicon {

// [rc4l] A `w` by `h` picture of four-byte texels (alpha last) averaged down to `size` by `size` in dst, each texel's colour weighted by its alpha so clear texels add no dark fringe; false when the picture is smaller than the size asked for.
bool ComputeIconDownscale(const unsigned char *src, int w, int h, int size, unsigned char *dst);

}} // namespace zx::appicon

#endif // ZX_APPICON_COMPUTE_H
