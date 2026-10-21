// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a texture's RGBA pixels say about it (GZDoom's averageColor, CheckTrans, SmoothEdges, FindHoles and global brightmap), for any renderer.

#ifndef ZX_TEXTUREPIXELS_COMPUTE_H
#define ZX_TEXTUREPIXELS_COMPUTE_H

namespace zx { namespace materials {

// [rc4l] The average colour of `count` RGBA texels as 0xFFRRGGBB, scaled so its brightest channel reaches `maxOut` when `scale`; white for no texels.
unsigned int ComputeAverageColor(const unsigned char *rgba, int count, bool scale, int maxOut);

// [rc4l] 1 when a picture has texels neither opaque nor clear, else 0; `trans` is what copying it already found, -1 when it does not know.
int ComputeTransparency(const unsigned char *rgba, int count, int trans);

// [rc4l] Give each clear texel beside an opaque one that neighbour's colour, still clear, so filtering blends no black in; true when the picture has clear texels.
bool ComputeSmoothEdges(unsigned char *rgba, int w, int h);

// [rc4l] The bands of rows holding opaque texels (start, length), a gap under 16 rows merged into the band above; 0 when there is nothing to save or more than four bands.
int ComputeTextureBands(const unsigned char *rgba, int w, int h, int bands[5][2]);

// [rc4l] Which palette entries a COLORMAP keeps at full light in all 32 levels, a near-black entry never counting; `lit[i]` set for each, their count returned.
int ComputeFullbrightEntries(const unsigned char *colormap, const unsigned char *playpal, bool lit[256]);

}} // namespace zx::materials

#endif // ZX_TEXTUREPIXELS_COMPUTE_H
