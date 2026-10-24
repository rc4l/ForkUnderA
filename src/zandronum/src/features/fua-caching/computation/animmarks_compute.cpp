// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "animmarks_compute.h"

namespace zx { namespace caching {

std::vector<int> ComputeAnimationFrames(bool discrete, int basePic, int numFrames, const std::vector<int> &listed, int count)
{
	std::vector<int> out;
	for (int i = 0; i < numFrames; i++)
	{
		const int pic = discrete ? (i < (int)listed.size() ? listed[i] : -1) : basePic + i;
		if (pic >= 0 && pic < count) out.push_back(pic);
	}
	return out;
}

void ComputeSpreadMarks(unsigned char *hitlist, int count, const std::vector<int> &frames)
{
	unsigned char marks = 0;
	for (int pic : frames)
		if (pic >= 0 && pic < count) marks |= hitlist[pic];
	if (marks == 0) return;
	for (int pic : frames)
		if (pic >= 0 && pic < count) hitlist[pic] |= marks;
}

}} // namespace zx::caching
