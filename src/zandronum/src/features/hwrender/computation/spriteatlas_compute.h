// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The sprite atlas's arithmetic: rectangles packed into fixed pages, where a sprite's texture coordinates land on its page, and which sprites may draw from one.

#ifndef ZX_HWRENDER_SPRITEATLAS_COMPUTE_H
#define ZX_HWRENDER_SPRITEATLAS_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] A row of a page, filled left to right with rectangles no taller than it.
struct AtlasShelf { int y, height, x; };
// [rc4l] A page's shelves, stacked from the top, and the first row below them.
struct AtlasPage { std::vector<AtlasShelf> shelves; int top = 0; };
// [rc4l] Where a rectangle went.
struct AtlasPlace { int page, x, y; };

// [rc4l] Places a w by h rectangle on the first page with room, on a shelf tall enough for it and at most twice its height, else on a new shelf, else on a new page up to maxPages; false when none has room.
bool ComputeAtlasPlace(std::vector<AtlasPage> &pages, int maxPages, int pageW, int pageH, int w, int h, AtlasPlace &out);

// [rc4l] Texture coordinate u, v of a w by h image at x, y of a pageW by pageH page, as the page's own coordinates.
void ComputeAtlasUV(int x, int y, int w, int h, int pageW, int pageH, float &u, float &v);

// [rc4l] What decides whether a dynamic piece draws from the atlas.
struct AtlasCandidate
{
	bool perMaterial;    // [rc4l] drawn with a binding per material, the path the atlas spares
	bool pointSampled;   // [rc4l] point sampled without mipmaps, so a one-texel gutter keeps neighbours apart
	bool sprite;         // [rc4l] a flat quad, not a decal, model or HUD surface
	bool plainTexture;   // [rc4l] not a camera texture, a warp or a hardware shader, which read their own coordinates
	int width, height;
};

// [Doruk-Aksoy] What a dynamic piece is, for whether it counts as a flat sprite the atlas may take.
struct AtlasPieceFacts
{
	bool quad, depthBias, solid, hud;
	int mirrorView, onMirror;   // [Doruk-Aksoy] -1 when the piece is in no mirror's view and on no mirror
	int fuzz;                   // [Doruk-Aksoy] its fuzz shader, 0 for none, whose noise and splat read the sprite's own texture coordinates
};

// [Doruk-Aksoy] Whether a piece is a plain flat sprite the atlas may take: a quad without depth bias, not solid, not on the HUD, outside every mirror and not fuzzy.
bool ComputeAtlasPieceEligible(const AtlasPieceFacts &f);

// [rc4l] Whether a piece draws from the atlas: all of the above and no side over maxSide texels.
bool ComputeAtlasEligible(const AtlasCandidate &c, int maxSide);

// [rc4l] A w by h RGBA image inside a one-texel transparent border, (w + 2) by (h + 2), so a point sample at its very edge never reads a neighbour.
void ComputeAtlasFramed(const unsigned char *rgba, int w, int h, std::vector<unsigned char> &out);

}} // namespace zx::hwrender

#endif // ZX_HWRENDER_SPRITEATLAS_COMPUTE_H
