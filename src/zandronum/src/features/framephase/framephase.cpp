// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/framephase/framephase.h"
#include "features/telemetry/telemetry.h"

#include <chrono>
#include <string>

namespace zx { namespace framephase {

namespace {

float g_now[kPhaseCount];
float g_last[kPhaseCount];
long long g_nowStart[kPhaseCount];
long long g_lastStart[kPhaseCount];
long long g_frameStart = 0;
long long g_lastFrameUs = 0;

// [rc4l] Each phase's timing counter in features/telemetry, named phase/<name>.
zx::telemetry::Counter PhaseCounter(int phase)
{
	static zx::telemetry::Counter counters[kPhaseCount];
	static const bool registered = [] {
		for (int p = 0; p < kPhaseCount; p++)
			counters[p] = zx::telemetry::Register(zx::telemetry::CounterSpec{ (std::string("phase/") + ComputePhaseName(p)).c_str(), "ms", zx::telemetry::CounterKind::Time, 0 });
		return true;
	}();
	(void)registered;
	return counters[phase];
}

} // namespace

long long NowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AddPhase(int phase, long long startUs, long long endUs)
{
	if (phase < 0 || phase >= kPhaseCount) return;
	g_now[phase] += (float)(endUs - startUs);
	zx::telemetry::Add(PhaseCounter(phase), (endUs - startUs) / 1000.0);
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
	zx::telemetry::FrameMark();
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
