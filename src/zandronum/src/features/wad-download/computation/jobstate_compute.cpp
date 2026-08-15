// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-download/computation/jobstate_compute.h"

namespace zx
{

JobStart JobDecideStart(bool bRunning, size_t workCount, JobWhenBusy whenBusy)
{
	// Asked FIRST, ahead of the busy test: an empty job is not worth deferring, and queueing one
	// would have a caller cancel the run in flight to make room for nothing.
	if (workCount == 0)
		return JobStart::Refuse;

	if (!bRunning)
		return JobStart::Start;

	return (whenBusy == JobWhenBusy::Defer) ? JobStart::Defer : JobStart::Refuse;
}

bool JobAcceptsBegin(bool bRunning, size_t workCount)
{
	return JobDecideStart(bRunning, workCount, JobWhenBusy::Refuse) == JobStart::Start;
}

bool JobAcceptsRescan(bool bRunning, bool bStarted, bool bForce)
{
	// Never two at once, whatever was asked for. Forcing a rebuild while one is running would leave
	// two workers writing the same list.
	if (bRunning)
		return false;

	return bForce || !bStarted;
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
