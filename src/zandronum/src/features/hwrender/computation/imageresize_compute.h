// [rc4l] Box-average RGBA image downscaler -- the GLU replacement from upstream Flight 1
// (94b06900c, code originally from wxWidgets). Used when a source texture exceeds the hardware's
// maximum size (rare in Doom). Pure so it is tested; the engine's texture uploader calls it
// instead of gluScaleImage, which removes the GLU dependency entirely.
//
// Arsenal note: first entry of the reusable-C++ arsenal -- any rebuilt upstream feature that needs
// CPU image scaling calls this, not a new copy.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_IMAGERESIZE_COMPUTE_H
#define ZX_IMAGERESIZE_COMPUTE_H

namespace zx
{

// [rc4l] Downscale an RGBA8 image with alpha-aware box averaging: fully transparent source pixels
// contribute no colour (prevents dark halos), and a box that is entirely transparent produces
// transparent black rather than upstream's division by zero. Buffers are tightly packed RGBA;
// dst must hold dstWidth*dstHeight*4 bytes. Intended for downscaling (dst <= src per axis).
void ResizeImageBoxAverage(int srcWidth, int srcHeight, const unsigned char *src,
	int dstWidth, int dstHeight, unsigned char *dst);

} // namespace zx

#endif // ZX_IMAGERESIZE_COMPUTE_H
