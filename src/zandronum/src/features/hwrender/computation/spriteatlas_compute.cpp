// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/spriteatlas_compute.h"

#include <cstddef>
#include <cstring>

namespace zx { namespace hwrender {

bool ComputeAtlasPlace(std::vector<AtlasPage> &pages, int maxPages, int pageW, int pageH, int w, int h, AtlasPlace &out)
{
	if (w <= 0 || h <= 0 || w > pageW || h > pageH) return false;
	for (int p = 0; p < maxPages; p++)
	{
		if (p == (int)pages.size()) pages.emplace_back();
		AtlasPage &page = pages[p];
		for (AtlasShelf &s : page.shelves)
		{
			if (s.height < h || s.height > h * 2 || s.x + w > pageW) continue;
			out = { p, s.x, s.y };
			s.x += w;
			return true;
		}
		if (page.top + h > pageH) continue;
		page.shelves.push_back({ page.top, h, w });
		out = { p, 0, page.top };
		page.top += h;
		return true;
	}
	return false;
}

void ComputeAtlasUV(int x, int y, int w, int h, int pageW, int pageH, float &u, float &v)
{
	u = (x + u * w) / (float)pageW;
	v = (y + v * h) / (float)pageH;
}

bool ComputeAtlasPieceEligible(const AtlasPieceFacts &f)
{
	return f.quad && !f.depthBias && !f.solid && !f.hud && f.mirrorView < 0 && f.onMirror < 0 && f.fuzz == 0;
}

bool ComputeAtlasEligible(const AtlasCandidate &c, int maxSide)
{
	return c.perMaterial && c.pointSampled && c.sprite && c.plainTexture && c.width > 0 && c.height > 0 && c.width <= maxSide && c.height <= maxSide;
}

void ComputeAtlasFramed(const unsigned char *rgba, int w, int h, std::vector<unsigned char> &out)
{
	const int fw = w + 2;
	out.assign((size_t)fw * (h + 2) * 4, 0);
	for (int y = 0; y < h; y++) memcpy(&out[((size_t)(y + 1) * fw + 1) * 4], rgba + (size_t)y * w * 4, (size_t)w * 4);
}

}} // namespace zx::hwrender
