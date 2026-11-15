// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida

#include "features/tic-pacing/computation/pacing_compute.h"

namespace zx { namespace ticpacing {

namespace {

// [Kaori-Chani] Tics a second in thousandths, taken in 64 bits because a long window's tics
// times a million passes what 32 holds.
unsigned int MilliHz(unsigned int tics, unsigned int ms)
{
	return (unsigned int)(((unsigned long long)tics * 1000000ull) / (unsigned long long)ms);
}

} // namespace

bool ComputeDue(unsigned int ms, int intervalSeconds)
{
	if (intervalSeconds <= 0)
	{
		return false;
	}

	// [Kaori-Chani] Compared in milliseconds, and in 64 bits so a long interval cannot overflow
	// into a window that is always due.
	return (unsigned long long)ms >= (unsigned long long)intervalSeconds * 1000ull;
}

PacingReport ComputeReport(unsigned int runTics, unsigned int clockTics, unsigned int ms)
{
	PacingReport report;
	report.runTics = runTics;
	report.clockTics = clockTics;
	report.ms = ms;
	report.runMilliHz = 0;
	// [Kaori-Chani] Signed and 64-bit: a loop can be ahead as well as behind, and a long stall
	// passes what an int holds.
	report.behindTics = (long long)clockTics - (long long)runTics;

	if (ms == 0)
	{
		return report;
	}

	report.runMilliHz = MilliHz(runTics, ms);

	return report;
}

}} // namespace zx::ticpacing
