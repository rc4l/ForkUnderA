// [rc4l] See replay_compute.h. Pure logic only — unit-tested at 100% line coverage off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/replay/computation/replay_compute.h"

#include <cstdio>

namespace zx {

bool ComputeFrameDue(int64_t lastCaptureUs, int64_t nowUs, int targetFps)
{
	if (targetFps <= 0)
		return false;
	const int64_t intervalUs = 1000000 / targetFps;
	return (nowUs - lastCaptureUs) >= intervalUs;
}

ScaledDims ComputeScaledDims(int srcW, int srcH, int maxH)
{
	int w = srcW;
	int h = srcH;
	if (maxH > 0 && srcH > maxH)
	{
		h = maxH;
		w = static_cast<int>(static_cast<int64_t>(srcW) * maxH / srcH);
	}
	// H.264 / NV12 need even dimensions; floor to even and keep a valid minimum surface.
	w &= ~1;
	h &= ~1;
	if (w < 2) w = 2;
	if (h < 2) h = 2;
	return ScaledDims{ w, h };
}

int ComputeRingCapacity(int durationSecs, int fps)
{
	if (durationSecs < 0) durationSecs = 0;
	if (fps < 1) fps = 1;
	return durationSecs * fps + 1;
}

void ComputeClipFilename(char *out, int outSize, const ClipStamp &s)
{
	if (out == nullptr || outSize <= 0)
		return;
	std::snprintf(out, static_cast<size_t>(outSize),
		"clip-%04d%02d%02d-%02d%02d%02d.mp4",
		s.year, s.month, s.day, s.hour, s.min, s.sec);
}

int ComputeClipStartIndex(const int64_t *tUs, const unsigned char *key, int count,
						  int64_t lastUs, int windowSecs)
{
	if (tUs == nullptr || key == nullptr || count <= 0)
		return -1;
	const int64_t cutoff = lastUs - static_cast<int64_t>(windowSecs) * 1000000;
	// Latest keyframe at/before the window start -> the clip is keyframe-aligned and covers the last
	// N seconds (a hair more, up to one GOP). Keep updating so we land on the newest such keyframe.
	int start = -1;
	for (int i = 0; i < count; ++i)
		if (key[i] && tUs[i] <= cutoff)
			start = i;
	if (start >= 0)
		return start;
	// Session shorter than the window: no keyframe sits before the cutoff, so start at the very
	// first keyframe (save whatever footage exists).
	for (int i = 0; i < count; ++i)
		if (key[i])
			return i;
	return -1; // nothing keyframed -> nothing decodable to save
}

} // namespace zx
