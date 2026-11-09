// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/server-browser/computation/downloadbar_compute.h"

#include <algorithm>

namespace zx
{

namespace
{

const unsigned int kSheenPeriodMs = 1600;
const unsigned int kBouncePeriodMs = 2000;

} // namespace

float ComputeDownloadFraction(long long received, long long total)
{
	if (total <= 0)
		return -1.f;
	const double f = static_cast<double>(std::max(0LL, received)) / static_cast<double>(total);
	return static_cast<float>(std::min(1.0, f));
}

NoticeStrip ComputeNoticeStrip(int headerBottom, int bandTop, int bandHeight)
{
	NoticeStrip out;
	out.top = (headerBottom > 0) ? headerBottom : bandTop;
	out.height = std::max(0, bandHeight);
	return out;
}

DownloadBarFill ComputeDownloadBarFill(int width, float fraction, unsigned int ms)
{
	DownloadBarFill out;
	const int w = std::max(0, width);

	if (fraction < 0.f)
	{
		// [Doruk-Aksoy] A triangle wave carries a quarter-width block from one end to the other and back.
		const int block = w / 4;
		const unsigned int phase = ms % kBouncePeriodMs;
		const unsigned int half = kBouncePeriodMs / 2;
		const unsigned int along = (phase < half) ? phase : (kBouncePeriodMs - phase);
		out.fillW = block;
		out.fillX = static_cast<int>((static_cast<long long>(w - block) * along) / half);
		out.sheenX = out.fillX;
		out.sheenW = 0;
		return out;
	}

	out.fillX = 0;
	out.fillW = static_cast<int>(w * std::min(1.f, fraction) + 0.5f);

	// [Doruk-Aksoy] The sheen enters from the left of the fill and leaves past its right edge, clipped to the fill.
	const int sheen = std::max(8, w / 12);
	const int travel = out.fillW + sheen;
	const int left = static_cast<int>((static_cast<long long>(travel) * (ms % kSheenPeriodMs)) / kSheenPeriodMs) - sheen;
	const int clippedLeft = std::max(0, left);
	const int clippedRight = std::min(out.fillW, left + sheen);
	out.sheenX = clippedLeft;
	out.sheenW = std::max(0, clippedRight - clippedLeft);
	return out;
}

} // namespace zx
