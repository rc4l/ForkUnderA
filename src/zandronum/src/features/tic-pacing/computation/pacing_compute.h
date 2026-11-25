// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [Kaori-Chani] The tic pacing report's arithmetic. Two tic counts, not one: what the time base's
// clock passed, and what the loop actually ran. Their difference is the only figure that tells a
// stalled clock from a loop too slow to keep up with a sound one.

#ifndef ZX_PACING_COMPUTE_H
#define ZX_PACING_COMPUTE_H

namespace zx { namespace ticpacing {

// [Kaori-Chani] One window's figures, every one an integer.
struct PacingReport
{
	unsigned int runTics;		// tics the loop ran
	unsigned int clockTics;		// tics the time base passed in the same window
	unsigned int ms;			// milliseconds the time base advanced
	unsigned int runMilliHz;	// tics run a second in thousandths, 35000 for exactly TICRATE
	long long behindTics;		// tics the clock passed that the loop did not run
};

// [Kaori-Chani] Whether a window that has run this long is due to report.
bool ComputeDue(unsigned int ms, int intervalSeconds);

// [Kaori-Chani] The window's figures; a span of no milliseconds leaves the rates at zero.
PacingReport ComputeReport(unsigned int runTics, unsigned int clockTics, unsigned int ms);

}} // namespace zx::ticpacing

#endif // ZX_PACING_COMPUTE_H
