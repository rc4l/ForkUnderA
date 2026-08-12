// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/menuart_compute.h"

namespace zx
{

std::string MenuArtFileName(const std::string &variantId)
{
	if (variantId.empty())
		return "art.png";

	return "art." + variantId + ".png";
}

std::vector<ArtRect> LayoutMenuArt(int slotX, int slotY, int slotW, int slotH, int gap,
                                   const std::vector<std::pair<int, int> > &sizes)
{
	std::vector<ArtRect> out;
	if (sizes.empty() || (slotW <= 0) || (slotH <= 0))
		return out;

	const int count = static_cast<int>(sizes.size());

	// What one picture may take. The gaps come out first so the shares are of what is actually left,
	// which is the difference between two pictures fitting and the second one hanging off the edge.
	const int gaps = gap * (count - 1);
	int share = (slotW - gaps) / count;
	if (share < 1)
		share = 1;

	int span = 0;

	for (int i = 0; i < count; ++i)
	{
		const int sw = (sizes[i].first > 0) ? sizes[i].first : 1;
		const int sh = (sizes[i].second > 0) ? sizes[i].second : 1;

		// Height first, because the slot's height is the thing being filled and a row of pictures
		// that are all the same height reads as a row. Width only ever takes some back.
		int h = slotH;
		int w = (sw * h) / sh;

		if (w > share)
		{
			// Too wide for its share, so it gives up height instead of crowding its neighbour. A
			// logo five times as wide as it is tall stays legible; a squashed one does not.
			w = share;
			h = (sh * w) / sw;
			if (h < 1)
				h = 1;
		}

		if (w < 1)
			w = 1;

		ArtRect r;
		r.w = w;
		r.h = h;
		out.push_back(r);

		span += w;
	}

	span += gaps;

	// Centred as a GROUP, so a narrow picture beside a wide one still sits in the middle of the slot
	// rather than the pair drifting to one side of it.
	int x = slotX + (slotW - span) / 2;

	for (int i = 0; i < count; ++i)
	{
		out[i].x = x;
		out[i].y = slotY + (slotH - out[i].h) / 2;
		x += out[i].w + gap;
	}

	return out;
}

} // namespace zx
