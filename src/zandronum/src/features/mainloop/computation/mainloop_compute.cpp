// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mainloop/computation/mainloop_compute.h"

namespace zx { namespace mainloop {

int ComputeWipeTicsDue(unsigned start, unsigned now)
{
	// [rc4l] Unsigned, as the clock is, so a wrap between the two still gives the time between them.
	return (int)((now - start) * 40 / 1000);
}

unsigned ComputeVblMilliseconds(int count)
{
	return count > 0 ? (unsigned)(1000 * count / 70) : 0;
}

bool ComputeDeadlinePassed(unsigned deadline, unsigned now)
{
	return (int)(now - deadline) >= 0;
}

}} // namespace zx::mainloop
