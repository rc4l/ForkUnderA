// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A run of frame times summed up the way frame rates are reported, with the percentiles and the 1% low where a stutter shows and the mean does not.

#ifndef ZX_PERFSTATS_COMPUTE_H
#define ZX_PERFSTATS_COMPUTE_H

#include <vector>

namespace zx { namespace perfstats {

// [rc4l] Frame times in milliseconds, and the average and 1% low (the worst 1% of frames averaged) as frames per second.
struct PerfSummary
{
	int    n;
	double mean, min, max, p50, p95, p99;
	double fpsAvg;
	double fps1pctLow;
};

// [rc4l] The summary of frameMs, all zero for none.
PerfSummary ComputeFrameTimeSummary(const std::vector<double> &frameMs);

}} // namespace zx::perfstats

#endif // ZX_PERFSTATS_COMPUTE_H
