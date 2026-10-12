// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A 2D texture draw as FGLRenderer::DrawTexture makes it from a DrawParms: where the quad lands, which part of the texture it shows, in what colour and texture mode.

#ifndef ZX_QUAD2D_COMPUTE_H
#define ZX_QUAD2D_COMPUTE_H

namespace zx { namespace draw2d {

// [rc4l] gl_interface.h's texture modes, which the glue checks these against.
enum { kTexModulate = 0, kTexMask = 1, kTexOpaque = 2, kTexInverse = 3, kTexRedToAlpha = 4 };
// [rc4l] renderstyle.h's STYLEF_ flags read here.
enum { kStyleRedIsAlpha = 4, kStyleColorIsFixed = 8, kStyleInvertSource = 16 };

// [rc4l] gl_GetRenderStyle's texture mode: opaque or modulated, unless the style samples red as alpha, fixes the colour, or inverts.
int ComputeTextureMode(int styleFlags, bool drawOpaque);

// [rc4l] What DrawTexture reads of a DrawParms and the material.
struct TextureQuadIn
{
	double x, y, left, top, destWidth, destHeight, texWidth, texHeight;
	double windowLeft, windowRight;
	bool   flipX;
	bool   canvas;                 // [rc4l] a camera texture, drawn whole and upside down
	float  ul, vt, ur, vb;         // [rc4l] the material's corner texture coordinates
	unsigned colorOverlay;         // [rc4l] ARGB
	unsigned fillColor;            // [rc4l] ARGB, the colour of a fixed-colour style
	bool   colorIsFixed;
	float  alpha;                  // [rc4l] 0..1
};

struct TextureQuadOut
{
	float x, y, w, h;              // [rc4l] as the 2D capture records them
	float x2, y2;                  // [rc4l] the far corner as GL's vertices hold it, summed before rounding
	float u1, v1, u2, v2;
	unsigned char r, g, b, a;
	unsigned colorOverlay;         // [rc4l] what is left of it once a black overlay became the light
};

void ComputeTextureQuad(const TextureQuadIn &in, TextureQuadOut &out);

}} // namespace zx::draw2d

#endif // ZX_QUAD2D_COMPUTE_H
