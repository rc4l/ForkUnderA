// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/ffblocks_compute.h"

namespace zx { namespace surfaces {

// [rc4l] Append a block unless it is upside down at both ends or there is no room left.
static void PushBlock(FFBlock *out, int &n, int maxOut, float topL, float topR, float botL, float botR, int rover)
{
	if (n >= maxOut || (topL < botL && topR < botR)) return;
	FFBlock &b = out[n++];
	b.top[0] = topL; b.top[1] = topR;
	b.bottom[0] = botL; b.bottom[1] = botR;
	b.rover = rover;
}

int ComputeFFClip(const float *top, const float *bottom, int kind, const FFRover *nearRovers, int nNear,
	int rover, FFBlock *out, int maxOut)
{
	int n = 0;
	float topL = top[0], topR = top[1];
	const float botL = bottom[0], botR = bottom[1];
	for (int i = 0; i < nNear; i++)
	{
		const FFRover &f = nearRovers[i];
		if (!f.renderSides || f.kind != kind) continue;
		// [rc4l] Wholly below the block: the slabs are top down, so nothing after this one can clip it.
		if (f.top[0] < botL && f.top[1] < botR) break;
		// [rc4l] Wholly above it.
		if (f.bottom[0] > topL && f.bottom[1] > topR) continue;
		if (f.top[0] >= topL && f.top[1] >= topR)
		{
			// [rc4l] Covering the block's top: all of it when it also reaches the bottom, else down to its own bottom.
			if (f.bottom[0] <= botL && f.bottom[1] <= botR) return n;
			if (f.bottom[0] > botL && f.bottom[1] > botR) { topL = f.bottom[0]; topR = f.bottom[1]; }
		}
		else if (f.top[0] <= topL && f.top[1] <= topR)
		{
			// [rc4l] Inside the block: the part above it is drawn, and the clip goes on below it.
			PushBlock(out, n, maxOut, topL, topR, f.top[0], f.top[1], rover);
			if (f.bottom[0] <= botL && f.bottom[1] <= botR) return n;
			topL = f.bottom[0]; topR = f.bottom[1];
		}
		// [rc4l] A slab crossing an edge of the block would need a split, which GL does not do either.
	}
	PushBlock(out, n, maxOut, topL, topR, botL, botR, rover);
	return n;
}

int ComputeFFBlocks(const float *wallTop, const float *wallBottom,
	const FFRover *rovers, int nRovers, FFBlock *out, int maxOut,
	const FFRover *nearRovers, int nNear)
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

		// [rc4l] Translucent and swimmable slabs give up what the near side's slabs of their kind already show.
		if (r.kind != 0)
		{
			const float t[2] = { ffTopL, ffTopR }, b[2] = { ffBotL, ffBotR };
			n += ComputeFFClip(t, b, r.kind, nearRovers, nNear, i, out + n, maxOut - n);
		}
		else PushBlock(out, n, maxOut, ffTopL, ffTopR, ffBotL, ffBotR, i);

		topL = ffBotL; topR = ffBotR;
		renderedSomething = true;
		// Everything below here is the wall's own lower part, which is already drawn.
		if (topL <= botL && topR <= botR) break;
	}
	return n;
}

}} // namespace zx::surfaces
