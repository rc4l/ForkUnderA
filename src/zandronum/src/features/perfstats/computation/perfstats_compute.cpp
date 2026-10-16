// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/perfstats/computation/perfstats_compute.h"

#include <algorithm>

namespace zx { namespace perfstats {

namespace {

// [rc4l] The nearest-rank percentile of a sorted, non-empty run, pct from 0 to 100.
double Percentile(const std::vector<double> &sorted, double pct)
{
	return sorted[(size_t)((pct / 100.0) * (double)(sorted.size() - 1) + 0.5)];
}

// [rc4l] A frame time in milliseconds as frames per second, zero for none.
double PerSecond(double ms)
{
	return ms > 0 ? 1000.0 / ms : 0.0;
}

} // namespace

PerfSummary ComputeFrameTimeSummary(const std::vector<double> &frameMs)
{
	PerfSummary s;
	s.n = (int)frameMs.size();
	s.mean = s.min = s.max = s.p50 = s.p95 = s.p99 = s.fpsAvg = s.fps1pctLow = 0.0;
	if (frameMs.empty()) return s;

	std::vector<double> v = frameMs;
	std::sort(v.begin(), v.end());
	double sum = 0.0;
	for (double x : v) sum += x;
	s.mean = sum / (double)v.size();
	s.min = v.front();
	s.max = v.back();
	s.p50 = Percentile(v, 50);
	s.p95 = Percentile(v, 95);
	s.p99 = Percentile(v, 99);
	s.fpsAvg = PerSecond(s.mean);

	const size_t worstCount = std::max<size_t>(1, v.size() / 100);
	double worstSum = 0.0;
	for (size_t i = v.size() - worstCount; i < v.size(); ++i) worstSum += v[i];
	s.fps1pctLow = PerSecond(worstSum / (double)worstCount);
	return s;
}

}} // namespace zx::perfstats
