// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida

#include "features/tic-pacing/pacing.h"
#include "features/tic-pacing/computation/pacing_compute.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "doomdef.h"	// TICRATE
#include "doomtype.h"	// Printf
#include "i_tictime.h"

// [Kaori-Chani] Seconds between tic pacing lines, 0 for none; fua_tictime prints one on demand.
CVAR(Int, fua_tictime_report, 0, 0)

namespace zx { namespace ticpacing {

namespace {

// [Kaori-Chani] The window: where the counter and the tic count stood when it opened, and the
// last tic count the loop reported, which is what the command prints between lines.
tictime_raw_t s_windowStart;
unsigned int s_windowTics;
unsigned int s_lastTics;
bool s_windowOpen;

void Open(tictime_raw_t now, unsigned int gametics)
{
	s_windowStart = now;
	s_windowTics = gametics;
	s_lastTics = gametics;
	s_windowOpen = true;
}

// [Kaori-Chani] Both figures come from the time base's own counter rather than from I_MSTime, so
// what is measured is the clock the tics are counted from.
PacingReport Window(tictime_raw_t now, unsigned int gametics)
{
	const tictime_raw_t span = now - s_windowStart;
	return ComputeReport(gametics - s_windowTics,
		(unsigned int)TIC_ScaleRaw(span, TICRATE),
		(unsigned int)TIC_ScaleRaw(span, 1000));
}

void Print(const PacingReport &report)
{
	Printf("tic pacing: %u of %u tics run in %u ms; clock %u.%03u/s, run %u.%03u/s (%d asked), "
		"%lld behind\n",
		report.runTics, report.clockTics, report.ms,
		report.clockMilliHz / 1000u, report.clockMilliHz % 1000u,
		report.runMilliHz / 1000u, report.runMilliHz % 1000u,
		TICRATE, report.behindTics);
}

} // namespace

void Tick(unsigned int gametics)
{
	const tictime_raw_t now = TIC_GetRawCounter();

	if (!s_windowOpen)
	{
		Open(now, gametics);
		return;
	}

	s_lastTics = gametics;

	const PacingReport report = Window(now, gametics);
	if (!ComputeDue(report.ms, *fua_tictime_report))
	{
		return;
	}

	Print(report);
	Open(now, gametics);
}

// [Kaori-Chani] The window so far, and a fresh one after it, for the command below.
static void PrintNow()
{
	if (!s_windowOpen)
	{
		Printf("tic pacing: the tic loop has not run yet\n");
		return;
	}

	const tictime_raw_t now = TIC_GetRawCounter();
	Print(Window(now, s_lastTics));
	Open(now, s_lastTics);
}

}} // namespace zx::ticpacing

CCMD(fua_tictime)
{
	zx::ticpacing::PrintNow();
}
