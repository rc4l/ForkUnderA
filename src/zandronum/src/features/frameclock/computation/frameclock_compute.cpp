// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frameclock/computation/frameclock_compute.h"

namespace zx { namespace frameclock {

long long ComputeCounterMicros(long long counter, long long frequency)
{
	if (frequency <= 0) return 0;
	return (counter / frequency) * 1000000 + (counter % frequency) * 1000000 / frequency;
}

long long ComputeSinceMsUs(long long nowUs, unsigned startMs)
{
	const unsigned nowMs = (unsigned)(nowUs / 1000);
	return (long long)(int)(nowMs - startMs) * 1000 + nowUs % 1000;
}

int ComputeTicFrac(long long sinceUs, long long lengthUs)
{
	if (lengthUs <= 0 || sinceUs >= lengthUs) return 65536;
	if (sinceUs <= 0) return 0;
	return (int)(sinceUs * 65536 / lengthUs);
}

}} // namespace zx::frameclock
