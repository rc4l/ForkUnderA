// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/framephase/framephase.h"

#include <chrono>

namespace zx { namespace framephase {

namespace {

float g_now[kPhaseCount];
float g_last[kPhaseCount];
long long g_nowStart[kPhaseCount];
long long g_lastStart[kPhaseCount];
long long g_frameStart = 0;
long long g_lastFrameUs = 0;

} // namespace

long long NowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AddPhase(int phase, long long startUs, long long endUs)
{
	if (phase < 0 || phase >= kPhaseCount) return;
	g_now[phase] += (float)(endUs - startUs);
	if (g_nowStart[phase] == 0) g_nowStart[phase] = startUs;
}

void FrameBoundary()
{
	const long long now = NowUs();
	if (g_frameStart != 0) g_lastFrameUs = now - g_frameStart;
	g_frameStart = now;
	for (int p = 0; p < kPhaseCount; p++)
	{
		g_last[p] = g_now[p]; g_now[p] = 0.f;
		g_lastStart[p] = g_nowStart[p]; g_nowStart[p] = 0;
	}
}

const float *LastFramePhases()
{
	return g_last;
}

const long long *LastFramePhaseStarts()
{
	return g_lastStart;
}

long long LastFrameUs()
{
	return g_lastFrameUs;
}

}} // namespace zx::framephase
