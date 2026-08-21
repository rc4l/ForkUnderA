// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/ffblocks_compute.h"

namespace zx { namespace surfaces {

int ComputeFFBlocks(const float *wallTop, const float *wallBottom,
	const FFRover *rovers, int nRovers, FFBlock *out, int maxOut)
{
	int n = 0;
	if (maxOut <= 0 || nRovers <= 0) return 0;

	// The running top: what the last slab left uncovered. It walks down the wall.
	float topL = wallTop[0], topR = wallTop[1];
	const float botL = wallBottom[0], botR = wallBottom[1];
	bool renderedSomething = false;

	for (int i = 0; i < nRovers && n < maxOut; i++)
	{
		const FFRover &r = rovers[i];
		// A slab that draws no sides contributes nothing, and an INVERTED one draws its sides from
		// the other direction -- that is the front sector's pass, not this one.
		if (!r.renderSides || r.invertSides) continue;

		float ffTopL = r.top[0], ffTopR = r.top[1];
		const float ffBotL = r.bottom[0], ffBotR = r.bottom[1];

		// Completely above the ceiling -- and only skippable while nothing has been drawn yet, which
		// is GL's own condition: once a block exists it is the running top that matters, not the
		// wall's.
		if (ffBotL > topL && ffBotR > topR && !renderedSomething) continue;

		// Overlapping what the previous slab already covered: clip to it.
		if (ffTopL > topL && ffTopR > topR)
		{
			ffTopL = topL;
			ffTopR = topR;
		}

		if (ffTopL >= ffBotL || ffTopR >= ffBotR)
		{
			FFBlock &b = out[n++];
			b.top[0] = ffTopL; b.top[1] = ffTopR;
			b.bottom[0] = ffBotL; b.bottom[1] = ffBotR;
			b.rover = i;
		}

		topL = ffBotL; topR = ffBotR;
		renderedSomething = true;
		// Everything below here is the wall's own lower part, which is already drawn.
		if (topL <= botL && topR <= botR) break;
	}
	return n;
}

}} // namespace zx::surfaces
