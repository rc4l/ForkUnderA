// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The engine's 2D draws worked out once, without GL: what a DCanvas call puts on the screen, as the 2D capture records it and GL draws it.

#ifndef ZX_DRAW2D_H
#define ZX_DRAW2D_H

#include "v_video.h"                       // [rc4l] DCanvas::DrawParms
#include "features/hwrender/hud2d.h"       // [rc4l] Quad2D

#include <vector>

class FTexture;

namespace zx { namespace draw2d {

// [rc4l] A DrawTexture call: the quad the capture records, the remap's palette index, and what GL draws with.
struct TextureDraw
{
	zx::hwrender::Quad2D quad;
	int      translation;               // [rc4l] the remap's palette index, 0 for none
	unsigned colorOverlay;              // [rc4l] ARGB drawn over the quad as a mask, 0 for none
	unsigned char r, g, b, a;           // [rc4l] the quad's colour
	float    x2, y2;                    // [rc4l] its far corner as GL's vertices take it
};

// [rc4l] Work out a DrawTexture call, as FGLRenderer::DrawTexture would draw it.
void PrepareTexture(FTexture *img, const DCanvas::DrawParms &parms, TextureDraw &out);

// [rc4l] The colour a line, pixel or clear is drawn in: `color` when it has one or palcolor is -1, else palette entry `palcolor`.
PalEntry DrawColor(int palcolor, uint32 color);

// [rc4l] An automap line from (x1, y1) to (x2, y2), a single pixel, a translucent dim and an opaque clear, as the capture records them.
zx::hwrender::Quad2D PrepareLine(int x1, int y1, int x2, int y2, PalEntry color);
zx::hwrender::Quad2D PreparePixel(int x, int y, PalEntry color);
zx::hwrender::Quad2D PrepareDim(PalEntry color, float amount, int x, int y, int w, int h);
zx::hwrender::Quad2D PrepareClear(int left, int top, int right, int bottom, PalEntry color);

// [rc4l] A FlatFill: the rectangle tiled with `src` from the screen's origin or its own, as one quad per copy of the texture; out is replaced.
void PrepareFlatFill(int left, int top, int right, int bottom, FTexture *src, bool localOrigin, std::vector<zx::hwrender::Quad2D> &out);

}} // namespace zx::draw2d

#endif // ZX_DRAW2D_H
