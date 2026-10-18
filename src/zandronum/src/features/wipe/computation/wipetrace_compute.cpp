// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wipe/computation/wipetrace_compute.h"

#include <stdlib.h>

namespace zx { namespace wipe {

void ComputeThumb(const unsigned char *rgb, int w, int h, Thumb &out)
{
	out.clear();
	if (rgb == nullptr || w <= 0 || h <= 0) return;
	out.resize(kThumbW * kThumbH * 3);
	for (int cy = 0; cy < kThumbH; cy++)
	{
		// [rc4l] A cell covers at least one pixel, so a frame narrower than the thumbnail repeats its pixels.
		const int y0 = cy * h / kThumbH, y1 = (cy + 1) * h / kThumbH > y0 ? (cy + 1) * h / kThumbH : y0 + 1;
		for (int cx = 0; cx < kThumbW; cx++)
		{
			const int x0 = cx * w / kThumbW, x1 = (cx + 1) * w / kThumbW > x0 ? (cx + 1) * w / kThumbW : x0 + 1;
			unsigned long sum[3] = { 0, 0, 0 };
			for (int y = y0; y < y1; y++)
				for (int x = x0; x < x1; x++)
					for (int c = 0; c < 3; c++) sum[c] += rgb[(y * w + x) * 3 + c];
			const unsigned long n = (unsigned long)(x1 - x0) * (y1 - y0);
			for (int c = 0; c < 3; c++) out[(cy * kThumbW + cx) * 3 + c] = (unsigned char)(sum[c] / n);
		}
	}
}

double ComputeThumbDistance(const Thumb &a, const Thumb &b)
{
	if (a.empty() || a.size() != b.size()) return -1.0;
	double sum = 0.0;
	for (size_t i = 0; i < a.size(); i++) sum += abs((int)a[i] - (int)b[i]);
	return sum / a.size();
}

double ComputeWipeDistances(const WipeRecord &wipe, std::vector<double> &toBefore, std::vector<double> &toAfter)
{
	toBefore.clear();
	toAfter.clear();
	for (const Thumb &frame : wipe.frames)
	{
		toBefore.push_back(ComputeThumbDistance(frame, wipe.before));
		toAfter.push_back(ComputeThumbDistance(frame, wipe.after));
	}
	return ComputeThumbDistance(wipe.before, wipe.after);
}

void WipeTraceLog::Frame(const Thumb &thumb, bool wiping)
{
	if (wiping)
	{
		if (!m_open)
		{
			if ((int)m_wipes.size() == kWipesKept) m_wipes.erase(m_wipes.begin());
			m_wipes.push_back(WipeRecord());
			m_wipes.back().before = m_last;
			m_open = true;
		}
		m_wipes.back().frames.push_back(thumb);
	}
	else if (m_open)
	{
		m_wipes.back().after = thumb;
		m_open = false;
	}
	m_last = thumb;
}

}} // namespace zx::wipe
