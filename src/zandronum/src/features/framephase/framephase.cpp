// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/framephase/framephase.h"

#include <chrono>

namespace zx { namespace framephase {

namespace {

float g_now[kPhaseCount];
float g_last[kPhaseCount];

} // namespace

long long NowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AddPhase(int phase, long long us)
{
	if (phase >= 0 && phase < kPhaseCount) g_now[phase] += (float)us;
}

void FrameBoundary()
{
	for (int p = 0; p < kPhaseCount; p++) { g_last[p] = g_now[p]; g_now[p] = 0.f; }
}

const float *LastFramePhases()
{
	return g_last;
}

}} // namespace zx::framephase
