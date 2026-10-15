// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/draw2d.h"

#include "features/draw2d/computation/quad2d_compute.h"
#include "features/draw2d/computation/flatfill_compute.h"
#include "features/materials/materials.h"
#include "features/levelmesh/captureshading.h"   // [rc4l] CapturePolyColor
#include "features/rendersettings/colormap.h"   // [rc4l] FColormap
#include "r_data/renderstyle.h"
#include "textures/textures.h"
#include "m_fixed.h"
#include "v_palette.h"   // [rc4l] GPalette

#include <string.h>

static_assert(STYLEOP_Add == zx::draw2d::kStyleOpAdd && STYLEALPHA_One == zx::draw2d::kStyleAlphaOne, "renderstyle.h must match quad2d_compute.h");

namespace zx { namespace draw2d {

// [rc4l] A palette colour as the 0xRRGGBB the record builders take.
static unsigned RGBOf(PalEntry color) { return (unsigned(color.r) << 16) | (unsigned(color.g) << 8) | color.b; }

PalEntry DrawColor(int palcolor, uint32 color)
{
	return ComputeDrawColorDirect(palcolor, color) ? PalEntry(color) : GPalette.BaseColors[palcolor];
}

zx::draw2d::Quad2D PrepareLine(int x1, int y1, int x2, int y2, PalEntry color)
{
	// [rc4l] The automap is made of these, and a backend that only replays rects drew its background and nothing else.
	return ComputeLineQuad(x1, y1, x2, y2, RGBOf(color));
}

zx::draw2d::Quad2D PreparePixel(int x, int y, PalEntry color)
{
	return ComputePixelQuad(x, y, RGBOf(color));
}

zx::draw2d::Quad2D PrepareDim(PalEntry color, float amount, int x, int y, int w, int h)
{
	// [rc4l] The menu's background dim and the pill behind every menu tab; material NULL means flat colour.
	return ComputeFillQuad((float)x, (float)y, (float)w, (float)h, RGBOf(color), amount);
}

zx::draw2d::Quad2D PrepareClear(int left, int top, int right, int bottom, PalEntry color)
{
	// [rc4l] A scissored glClear is still a filled rectangle as far as a backend is concerned: the console background and the menu backdrop.
	return ComputeFillQuad((float)left, (float)top, (float)(right - left), (float)(bottom - top), RGBOf(color), 1.f);
}

bool PreparePoly(FTexture *tex, const FVector2 *points, int npoints, double originx, double originy, double scalex, double scaley,
	angle_t rotation, FDynamicColormap *colormap, int lightlevel, PolyDraw &draw, std::vector<zx::draw2d::Quad2D> &tris)
{
	tris.clear();
	if (npoints < 3) return false;
	draw.material = zx::materials::MaterialForTexture(tex, false);
	if (draw.material == NULL) return false;
	FColormap cm;
	cm = colormap;
	zx::levelmesh::CapturePolyColor(lightlevel, cm, draw.r, draw.g, draw.b);
	draw.points.resize(2 * npoints);
	draw.uv.resize(2 * npoints);
	for (int i = 0; i < npoints; i++)
	{
		draw.points[2 * i] = points[i].X;
		draw.points[2 * i + 1] = points[i].Y;
	}
	const PolyTexIn in = { originx, originy, scalex, scaley, rotation, tex->GetScaledWidth(), tex->GetScaledHeight(), tex->bHasCanvas };
	ComputePolyTexCoords(in, &draw.points[0], npoints, &draw.uv[0]);
	zx::draw2d::Quad2D base = ComputeFillQuad(0.f, 0.f, 0.f, 0.f, 0xffffffu, 1.f);
	base.material = draw.material;
	base.r = draw.r;
	base.g = draw.g;
	base.b = draw.b;
	ComputePolyTriangles(base, &draw.points[0], &draw.uv[0], npoints, tris);
	return true;
}

void PrepareFlatFill(int left, int top, int right, int bottom, FTexture *src, bool localOrigin, std::vector<zx::draw2d::Quad2D> &out)
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
		zx::draw2d::Quad2D q = ComputeFillQuad(t.x, t.y, t.w, t.h, 0xffffffu, 1.f);
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

	zx::draw2d::Quad2D &d = out.quad;
	d = ComputeTextureQuadRecord(q);
	d.material = mat;
	d.clipL = parms.lclip; d.clipT = parms.uclip; d.clipR = parms.rclip; d.clipB = parms.dclip;
	d.blend = ComputeQuadBlend(parms.style.BlendOp, parms.style.DestAlpha);
	// [rc4l] Negated: the backend reads a negative value as an internal palette index, as GL binding a material does (gl_material.cpp), not a game translation.
	d.translation = -out.translation;
	d.texMode = img->bHasCanvas ? kTexOpaque : ComputeTextureMode(parms.style.Flags, !parms.masked);
}

}} // namespace zx::draw2d
