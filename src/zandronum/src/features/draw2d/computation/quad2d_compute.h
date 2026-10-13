// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A 2D texture draw as FGLRenderer::DrawTexture makes it from a DrawParms: where the quad lands, which part of the texture it shows, in what colour and texture mode.

#ifndef ZX_QUAD2D_COMPUTE_H
#define ZX_QUAD2D_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] The 2D capture's record, one quad in the engine's 2D space (origin top-left, y down, as Begin2D sets it): features/draw2d works it out and the backend draws it.
struct Quad2D
{
	const void *material;              // FMaterial*, identity only; NULL means an untextured fill
	float       x, y, w, h;            // destination rect, screen pixels
	float       u1, v1, u2, v2;        // source rect, already flipped/windowed by the caller
	float       r, g, b, a;            // colour modulation, 0..1
	int         clipL, clipT, clipR, clipB;   // scissor, screen pixels; clipR <= clipL means none
	int         blend;                 // [rc4l] a kBlend2D* value
	// [rc4l] Palette remap index in FMaterial::CreateTexBuffer's convention, which is what makes coloured text a different colour from the same glyph.
	int         translation;
	// [rc4l] TexMode from gl_interface.h, where coloured text's TM_MASK takes only coverage from the texture and RGB from the vertex colour.
	int         texMode;
	// [rc4l] What this record is, a kQuad* value: the automap's lines are diagonal and interleave with the text drawn over them, so they ride the same list.
	int         kind;
	float       lx2, ly2;
	// [rc4l] A triangle's third corner and its texture coordinate; its first is (x, y) at (u1, v1) and its second (lx2, ly2) at (u2, v2).
	float       x3, y3, u3, v3;
	// [rc4l] Which window it is for, a k2D target, stamped by Record2D from Set2DTarget.
	int         target;
	// [rc4l] The fixed colormap (gl_fixedcolormap) a weapon layer is drawn under, as GL draws the weapon before resetting it for the HUD; 0 for everything else.
	int         fixedColormap;
	// [rc4l] The fuzz shader a fuzzy weapon layer draws with under gl_fuzztype (fuzz_compute's kFuzz*), 0 for everything else.
	int         fuzz;
};

}} // namespace zx::hwrender

namespace zx { namespace draw2d {

// [rc4l] gl_interface.h's texture modes, which the glue checks these against.
enum { kTexModulate = 0, kTexMask = 1, kTexOpaque = 2, kTexInverse = 3, kTexRedToAlpha = 4 };
// [rc4l] renderstyle.h's STYLEF_ flags read here.
enum { kStyleRedIsAlpha = 4, kStyleColorIsFixed = 8, kStyleInvertSource = 16 };

// [rc4l] Whether a line, pixel or clear is drawn in its own colour rather than palette entry palcolor: when it has one, or palcolor is -1, which a clear to black passes with colour 0.
bool ComputeDrawColorDirect(int palcolor, unsigned color);

// [rc4l] gl_GetRenderStyle's texture mode: opaque or modulated, unless the style samples red as alpha, fixes the colour, or inverts.
int ComputeTextureMode(int styleFlags, bool drawOpaque);

// [rc4l] renderstyle.h's ERenderOp and ERenderAlpha values read here.
enum { kStyleOpAdd = 1, kStyleAlphaOne = 1 };
// [rc4l] The 2D capture's blends: GL's usual source-alpha over, source-alpha added, and the screen tint's multiply.
enum { kBlend2DAlpha = 0, kBlend2DAdditive = 1, kBlend2DMultiply = 2 };
// [rc4l] What a record is: a textured or solid rectangle, a line, or one triangle of a polygon, whose texture repeats.
enum { kQuadRect = 0, kQuadLine = 1, kQuadTri = 2 };

// [rc4l] An untextured rectangle at (x, y), w by h, in 0xRRGGBB at `alpha`, with no scissor.
zx::hwrender::Quad2D ComputeFillQuad(float x, float y, float w, float h, unsigned rgb, float alpha);
// [rc4l] An automap line from (x1, y1) to (x2, y2) in 0xRRGGBB.
zx::hwrender::Quad2D ComputeLineQuad(int x1, int y1, int x2, int y2, unsigned rgb);
// [rc4l] One pixel in 0xRRGGBB.
zx::hwrender::Quad2D ComputePixelQuad(int x, int y, unsigned rgb);
// [rc4l] The screen's tint over a w by h screen, multiplied or blended over.
zx::hwrender::Quad2D ComputeScreenFillQuad(float w, float h, float r, float g, float b, float a, bool multiply);

// [rc4l] A FillSimplePoly as GL draws it: the material, the colour, and each point (x, y pairs) with its texture coordinate (u, v pairs).
struct PolyDraw
{
	const void *material;
	float r, g, b;
	std::vector<float> points, uv;
};

// [rc4l] What FillSimplePoly reads to texture a polygon: the texture's origin and scale in screen space, its turn (BAM), its scaled size, and whether it is a camera texture, drawn upside down.
struct PolyTexIn
{
	double originX, originY, scaleX, scaleY;
	unsigned rotation;
	int textureWidth, textureHeight;
	bool canvas;
};

// [rc4l] FillSimplePoly's texture coordinates for `count` points (x, y pairs) into `uv` (u, v pairs): each point's offset from the origin, turned, over the texture's scaled size.
void ComputePolyTexCoords(const PolyTexIn &in, const float *points, int count, float *uv);

// [rc4l] A polygon's fan as triangles, each a copy of `base` with its corners from `points` and `uv`, into `out` (replaced): none under three points.
void ComputePolyTriangles(const zx::hwrender::Quad2D &base, const float *points, const float *uv, int count, std::vector<zx::hwrender::Quad2D> &out);

// [rc4l] The 2D blend gl_GetRenderStyle sets for a style: additive when it adds onto the screen whole, alpha otherwise.
int ComputeQuadBlend(int blendOp, int destAlpha);

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
