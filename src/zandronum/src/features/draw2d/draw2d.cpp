// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/draw2d.h"

#include "features/draw2d/computation/quad2d_compute.h"
#include "features/draw2d/computation/flatfill_compute.h"
#include "features/materials/materials.h"
#include "r_data/renderstyle.h"
#include "textures/textures.h"
#include "m_fixed.h"
#include "v_palette.h"   // GPalette

#include <string.h>

static_assert(STYLEOP_Add == zx::draw2d::kStyleOpAdd && STYLEALPHA_One == zx::draw2d::kStyleAlphaOne, "renderstyle.h must match quad2d_compute.h");

namespace zx { namespace draw2d {

// [rc4l] An untextured record of the rectangle at (x, y), w by h, in `color` at `alpha`, with no scissor.
static zx::hwrender::Quad2D Fill(float x, float y, float w, float h, PalEntry color, float alpha)
{
	zx::hwrender::Quad2D q;
	memset(&q, 0, sizeof(q));
	q.x = x; q.y = y; q.w = w; q.h = h;
	q.u2 = 1.f; q.v2 = 1.f;
	q.r = color.r / 255.f; q.g = color.g / 255.f; q.b = color.b / 255.f; q.a = alpha;
	return q;
}

PalEntry DrawColor(int palcolor, uint32 color)
{
	return color ? PalEntry(color) : GPalette.BaseColors[palcolor];
}

zx::hwrender::Quad2D PrepareLine(int x1, int y1, int x2, int y2, PalEntry color)
{
	// [rc4l] The automap is made of these, and a backend that only replays rects drew its background and nothing else.
	zx::hwrender::Quad2D q = Fill((float)x1, (float)y1, 0.f, 0.f, color, 1.f);
	q.u2 = q.v2 = 0.f;
	q.kind = 1;
	q.lx2 = (float)x2; q.ly2 = (float)y2;
	return q;
}

zx::hwrender::Quad2D PreparePixel(int x, int y, PalEntry color)
{
	zx::hwrender::Quad2D q = Fill((float)x, (float)y, 1.f, 1.f, color, 1.f);
	q.u2 = q.v2 = 0.f;
	return q;
}

zx::hwrender::Quad2D PrepareDim(PalEntry color, float amount, int x, int y, int w, int h)
{
	// [rc4l] The menu's background dim and the pill behind every menu tab; material NULL means flat colour.
	return Fill((float)x, (float)y, (float)w, (float)h, color, amount);
}

zx::hwrender::Quad2D PrepareClear(int left, int top, int right, int bottom, PalEntry color)
{
	// [rc4l] A scissored glClear is still a filled rectangle as far as a backend is concerned: the console background and the menu backdrop.
	return Fill((float)left, (float)top, (float)(right - left), (float)(bottom - top), color, 1.f);
}

void PrepareFlatFill(int left, int top, int right, int bottom, FTexture *src, bool localOrigin, std::vector<zx::hwrender::Quad2D> &out)
{
	out.clear();
	const zx::materials::MaterialRef mat = zx::materials::MaterialForTexture(src, false);
	if (mat == NULL) return;
	float ul = 0.f, vt = 0.f, ur = 1.f, vb = 1.f;
	zx::materials::MaterialCorners(mat, ul, vt, ur, vb);
	std::vector<FlatTile> tiles;
	ComputeFlatTiles(left, top, right, bottom, src->GetWidth(), src->GetHeight(), localOrigin, tiles);
	for (size_t i = 0; i < tiles.size(); i++)
	{
		const FlatTile &t = tiles[i];
		zx::hwrender::Quad2D q = Fill(t.x, t.y, t.w, t.h, PalEntry(255, 255, 255), 1.f);
		q.material = mat;
		q.u1 = ul + t.u1 * (ur - ul); q.u2 = ul + t.u2 * (ur - ul);
		q.v1 = vt + t.v1 * (vb - vt); q.v2 = vt + t.v2 * (vb - vt);
		q.texMode = kTexModulate;
		out.push_back(q);
	}
}

void PrepareTexture(FTexture *img, const DCanvas::DrawParms &parms, TextureDraw &out)
{
	const zx::materials::MaterialRef mat = zx::materials::MaterialForTexture(img, false);
	TextureQuadIn in;
	in.x = parms.x; in.y = parms.y; in.left = parms.left; in.top = parms.top;
	in.destWidth = parms.destwidth; in.destHeight = parms.destheight;
	in.texWidth = parms.texwidth; in.texHeight = parms.texheight;
	in.windowLeft = parms.windowleft; in.windowRight = parms.windowright;
	in.flipX = parms.flipX != 0;
	in.canvas = img->bHasCanvas;
	in.ul = 0.f; in.vt = 0.f; in.ur = 1.f; in.vb = 1.f;
	zx::materials::MaterialCorners(mat, in.ul, in.vt, in.ur, in.vb);
	in.colorOverlay = parms.colorOverlay;
	in.fillColor = parms.fillcolor;
	in.colorIsFixed = (parms.style.Flags & STYLEF_ColorIsFixed) != 0;
	in.alpha = FIXED2FLOAT(parms.alpha);
	TextureQuadOut q;
	ComputeTextureQuad(in, q);

	// [rc4l] A remap counts only on a paletted texture that is not a camera's.
	out.translation = (!img->bHasCanvas && !parms.alphaChannel) ? zx::materials::MaterialRemapIndex(parms.remap) : 0;
	out.colorOverlay = q.colorOverlay;
	out.r = q.r; out.g = q.g; out.b = q.b; out.a = q.a;
	out.x2 = q.x2; out.y2 = q.y2;

	zx::hwrender::Quad2D &d = out.quad;
	d.material = mat;
	d.x = q.x; d.y = q.y; d.w = q.w; d.h = q.h;
	d.u1 = q.u1; d.v1 = q.v1; d.u2 = q.u2; d.v2 = q.v2;
	d.r = q.r / 255.f; d.g = q.g / 255.f; d.b = q.b / 255.f; d.a = q.a / 255.f;
	d.clipL = parms.lclip; d.clipT = parms.uclip; d.clipR = parms.rclip; d.clipB = parms.dclip;
	d.blend = ComputeQuadBlend(parms.style.BlendOp, parms.style.DestAlpha);
	// [rc4l] Negated: the backend reads a negative value as an internal palette index, as FGLTexture::Bind does, not a game translation.
	d.translation = -out.translation;
	d.texMode = img->bHasCanvas ? kTexOpaque : ComputeTextureMode(parms.style.Flags, !parms.masked);
	d.kind = 0;
	d.lx2 = d.ly2 = 0.f;
	d.target = 0;
}

}} // namespace zx::draw2d
