// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the backend presented around each wipe, for fua_wipe_report: every frame shrunk to a thumbnail, each wipe's frames kept with the one before and the one after, and how far each frame sits from those two.

#ifndef ZX_WIPETRACE_COMPUTE_H
#define ZX_WIPETRACE_COMPUTE_H

#include <vector>

namespace zx { namespace wipe {

enum { kThumbW = 32, kThumbH = 20, kWipesKept = 4 };

typedef std::vector<unsigned char> Thumb;

// [rc4l] A w by h top-down RGB frame shrunk to kThumbW by kThumbH RGB, each cell the mean of the pixels it covers; empty for an empty frame.
void ComputeThumb(const unsigned char *rgb, int w, int h, Thumb &out);

// [rc4l] The mean difference of two thumbnails' channels, 0 to 255, or -1 when either is missing or they differ in size.
double ComputeThumbDistance(const Thumb &a, const Thumb &b);

// [rc4l] One wipe as the backend presented it: the frame before it (empty when there was none), each frame while it ran, and the first after it (empty until one comes).
struct WipeRecord { Thumb before, after; std::vector<Thumb> frames; };

// [rc4l] How far each of a wipe's frames sits from its before and its after, and the distance between those two, which it returns.
double ComputeWipeDistances(const WipeRecord &wipe, std::vector<double> &toBefore, std::vector<double> &toAfter);

// [rc4l] Wipes recorded from the frames the backend presents, the last kWipesKept kept: a frame while one runs joins it, the first after closes it, and every frame is the next wipe's before.
class WipeTraceLog
{
public:
	void NoteStarted() { m_started++; }
	void Frame(const Thumb &thumb, bool wiping);
	int Started() const { return m_started; }
	const std::vector<WipeRecord> &Wipes() const { return m_wipes; }

private:
	Thumb m_last;
	std::vector<WipeRecord> m_wipes;
	bool m_open = false;
	int m_started = 0;
};

}} // namespace zx::wipe

#endif // ZX_WIPETRACE_COMPUTE_H
