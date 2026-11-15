//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: i_tictime_wait_test.cpp
//
// Description: The tic wait, whichever of i_tictime_wait.cpp and
// i_tictime_web.cpp this build compiles. Its own file beside i_tictime_wait.cpp
// so that coverage.sh --auto enrols the wait as well as the arithmetic, the two
// having been split apart to keep the browser's condition out of the time base.
//
// Neither case asserts how promptly the wait returns. A shared CI runner, and
// macOS grouping timer wake-ups to save power, make wake precision something
// the live runs and the pacing job measure rather than something a unit test
// can assert without going red for reasons outside this code.
//
//-----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include "i_tictime.h"

// A deadline already behind the counter must cost nothing, which is what the caller's loop
// relies on when it recomputes and calls again.
TEST(TicTimeWait, APassedDeadlineReturnsAtOnce)
{
	TIC_Init();
	const tictime_raw_t before = TIC_GetRawCounter();
	TIC_SleepUntilRaw(before);
	TIC_SleepUntilRaw(before > 1000 ? before - 1000 : 0);
	const tictime_raw_t spent = TIC_ScaleRaw(TIC_GetRawCounter() - before, 1000);
	// Loose on purpose: this catches a wait that happened, not how promptly it returned.
	EXPECT_LT(spent, 1000u) << "a passed deadline should not have waited";
}

// And a deadline ahead of it is actually reached, on a build whose wait waits: the browser's is
// a no-op by design, a page driving the loop a frame at a time, and it is asked rather than
// assumed so that this file needs no condition of its own.
TEST(TicTimeWait, AFutureDeadlineIsWaitedOut)
{
	TIC_Init();
	const tictime_raw_t freq = TIC_GetRawFrequency();
	const tictime_raw_t deadline = TIC_GetRawCounter() + freq / 100ull;	// 10 ms
	TIC_SleepUntilRaw(deadline);
	if (!TIC_SleepWaits())
	{
		SUCCEED() << "this build's wait is a no-op by design; it returned, which is all it owes";
		return;
	}
	EXPECT_GE(TIC_GetRawCounter(), deadline) << "the wait returned before its deadline";
	// The upper bound only catches a wait that never returns.
	const tictime_raw_t over = TIC_ScaleRaw(TIC_GetRawCounter() - deadline, 1000);
	EXPECT_LT(over, 1000u) << "overshot the deadline by " << over << " ms";
}

static int s_reports;
static const char* s_path;
static void RecordPath(const char* path) { s_reports++; s_path = path; }

// The first real wait names its path once, which holds in any order since no other test installs
// a hook.
TEST(TicTimeWait, TheFirstRealWaitNamesItsPathOnce)
{
	TIC_Init();
	s_reports = 0;
	TIC_WaitPathReport = RecordPath;
	const tictime_raw_t ms = TIC_GetRawFrequency() / 1000ull;
	TIC_SleepUntilRaw(TIC_GetRawCounter() + ms);
	TIC_SleepUntilRaw(TIC_GetRawCounter() + ms);
	TIC_WaitPathReport = NULL;
	if (!TIC_SleepWaits())
	{
		EXPECT_EQ(0, s_reports) << "the browser's wait never waits, so it has nothing to name";
		return;
	}
	EXPECT_EQ(1, s_reports);
	ASSERT_NE(nullptr, s_path);
	EXPECT_STRNE("", s_path);
}
