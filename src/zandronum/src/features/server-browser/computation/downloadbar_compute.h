// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The download bar along the top of the screen: where it sits, how full it is, and its moving highlight.
#ifndef ZX_DOWNLOADBAR_COMPUTE_H
#define ZX_DOWNLOADBAR_COMPUTE_H

namespace zx
{

// [Doruk-Aksoy] Received over total in [0, 1], or -1 while the server has not said how big the file is.
float ComputeDownloadFraction(long long received, long long total);

// [Doruk-Aksoy] The strip the bar paints, in screen pixels.
struct NoticeStrip
{
	int top;
	int height;
};

// [Doruk-Aksoy] Over the whole header bar when one is showing (coverBottom > 0), so no tab peeks through, else the plain band.
NoticeStrip ComputeNoticeStrip(int coverBottom, int bandTop, int bandHeight);

// [Doruk-Aksoy] The filled part and the highlight inside it, in pixels from the strip's left edge.
struct DownloadBarFill
{
	int fillX, fillW;
	int sheenX, sheenW;
};

// [Doruk-Aksoy] A known fraction fills from the left with a sheen sweeping across it; an unknown one bounces a block end to end.
DownloadBarFill ComputeDownloadBarFill(int width, float fraction, unsigned int ms);

} // namespace zx

#endif // ZX_DOWNLOADBAR_COMPUTE_H
