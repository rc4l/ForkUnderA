// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame-limit/computation/framelimit_compute.h"

namespace zx { namespace framelimit {

namespace {

const long long kSecondUs = 1000000;
const long long kYieldWindowUs = 2000;

}

int ComputeFrameLimitFps(int maxfps, bool capfps, int ticrate)
{
	if (capfps) return ticrate;
	return maxfps > 0 ? maxfps : 0;
}

long long ComputeFrameDeadline(long long previousUs, long long nowUs, int fps)
{
	if (fps <= 0 || previousUs <= 0) return nowUs;
	const long long period = kSecondUs / fps;
	const long long next = previousUs + period;
	if (next < nowUs - period || next > nowUs + kSecondUs) return nowUs;
	return next;
}

long long ComputeFrameLimitSleepUs(long long deadlineUs, long long nowUs)
{
	const long long wait = deadlineUs - nowUs;
	if (wait <= 0 || wait > kSecondUs) return -1;
	return wait <= kYieldWindowUs ? 0 : wait - kYieldWindowUs;
}

bool ComputeSteppedFrameDue(long long deadlineUs, long long nowUs, long long toleranceUs)
{
	return nowUs + toleranceUs >= deadlineUs;
}

}} // namespace zx::framelimit
