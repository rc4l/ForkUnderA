// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The bindless material slots dynamic pieces asked for lately, for the gpu/slots/ counters.

#ifndef ZX_SLOTREPORT_COMPUTE_H
#define ZX_SLOTREPORT_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] When a dynamic piece last asked for a slot: the frame and the time in ms, frame -1 for never.
struct SlotStamp
{
	long long frame = -1;
	double ms = 0;
};

// [Doruk-Aksoy] Adds to counts (sized here to one more than windowsMs) the stamps of `frame`, then those within each window of the last windowsMs[i] ms up to nowMs.
void ComputeWorkingSet(const std::vector<SlotStamp> &stamps, long long frame, double nowMs, const std::vector<double> &windowsMs, std::vector<int> &counts);

// [Doruk-Aksoy] Raises each peak to its count, growing peaks to as many counts as there are.
void ComputeKeepPeaks(std::vector<int> &peaks, const std::vector<int> &counts);

}} // namespace zx::hwrender

#endif // ZX_SLOTREPORT_COMPUTE_H
