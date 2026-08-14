// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-download/computation/jobstate_compute.h"

namespace zx
{

bool JobAcceptsBegin(bool bRunning, size_t workCount)
{
	if (bRunning)
		return false;

	return workCount > 0;
}

bool JobAcceptsResult(int resultEpoch, int currentEpoch)
{
	// [rc4l] A negative result epoch is "nothing arrived", which every drain reports the same way.
	// Answering true for it would have the caller apply an empty result as if it were an answer.
	if (resultEpoch < 0)
		return false;

	return resultEpoch == currentEpoch;
}

int JobNextEpoch(int epoch, const std::vector<int> &previous, const std::vector<int> &now,
                 bool &bChanged)
{
	bChanged = (previous.size() != now.size());

	for (size_t i = 0; !bChanged && (i < now.size()); ++i)
	{
		if (previous[i] != now[i])
			bChanged = true;
	}

	return bChanged ? (epoch + 1) : epoch;
}

} // namespace zx
