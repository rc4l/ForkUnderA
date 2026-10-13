// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/framephase/computation/framephase_compute.h"

namespace zx { namespace framephase {

const char *ComputePhaseName(int phase)
{
	static const char *const kNames[kPhaseCount] = { "sim", "walk", "rebake", "actors", "backend", "upload", "lights", "present", "input", "display", "flip" };
	return phase >= 0 && phase < kPhaseCount ? kNames[phase] : "?";
}

int ComputeSlowFramePhases(const float *phases, const double *frameUs, int n, double slowUs, double *slowMean, double *restMean)
{
	int slow = 0;
	for (int p = 0; p < kPhaseCount; p++) slowMean[p] = restMean[p] = 0.0;
	for (int i = 0; i < n; i++)
	{
		const bool isSlow = frameUs[i] > slowUs;
		if (isSlow) slow++;
		for (int p = 0; p < kPhaseCount; p++) (isSlow ? slowMean : restMean)[p] += phases[i * kPhaseCount + p];
	}
	for (int p = 0; p < kPhaseCount; p++)
	{
		if (slow > 0) slowMean[p] /= slow;
		if (n - slow > 0) restMean[p] /= (n - slow);
	}
	return slow;
}

}} // namespace zx::framephase
