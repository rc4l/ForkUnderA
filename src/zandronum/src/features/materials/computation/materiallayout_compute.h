// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a material's picture sits and how big it is drawn, as GL's FMaterial constructor and TrimBorders worked it out, for any renderer.

#ifndef ZX_MATERIALLAYOUT_COMPUTE_H
#define ZX_MATERIALLAYOUT_COMPUTE_H

namespace zx { namespace materials {

// [rc4l] A material's size and offsets in texels (a sprite's transparent frame included), the size walls and flats draw it at, and a sprite's rectangle about its origin with the texture coordinates of its trimmed edges.
struct MaterialLayout
{
	int width, height;
	int leftOffset, topOffset;
	int renderWidth, renderHeight;
	float spriteU[2], spriteV[2];
	float spriteLeft, spriteTop, spriteWidth, spriteHeight;
};

// [rc4l] A texture's own size, offsets and drawn size, and its scale in texels per unit.
struct MaterialSource
{
	int width, height;
	int leftOffset, topOffset;
	int renderWidth, renderHeight;
	float xScale, yScale;
};

// [rc4l] The layout of `src`, framed by one transparent texel when `expanded`, its sprite cut to `trim` (left, top, width, height in unframed texels) when `trimmed`.
MaterialLayout ComputeMaterialLayout(const MaterialSource &src, bool expanded, bool trimmed, const int trim[4]);

// [rc4l] The bounds of an RGBA picture's opaque texels (left, top, width, height): a fully transparent one trims to its corner texel, and a single texel is not trimmed at all.
bool ComputeTrimBorders(const unsigned char *rgba, int w, int h, int rect[4]);

// [rc4l] The size a texture side is stored at on a device whose largest is `maxSize` (none when 0 or less).
int ComputeStoredDimension(int value, int maxSize);

}} // namespace zx::materials

#endif // ZX_MATERIALLAYOUT_COMPUTE_H
