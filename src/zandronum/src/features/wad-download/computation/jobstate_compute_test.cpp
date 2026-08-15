// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/wad-download/computation/jobstate_compute.h"

using namespace zx;

using zx::JobStart;
using zx::JobWhenBusy;

namespace
{

std::vector<int> Counters(int a, int b)
{
	std::vector<int> out;
	out.push_back(a);
	out.push_back(b);
	return out;
}

} // namespace

// ---------------------------------------------------------------- starting

TEST(JobState, WillNotStartASecondRunOverTheFirst)
{
	// The whole reason a draw can ask every frame: the second ask through is a bool test rather
	// than another thread reading the same files.
	EXPECT_FALSE(JobAcceptsBegin(true, 5));
	EXPECT_TRUE(JobAcceptsBegin(false, 5));
}

TEST(JobState, WillNotStartWithNothingToDo)
{
	// A thread that does nothing still has to be created and scheduled. "Everything is already
	// answered" is the ordinary state of this screen, so it must not cost one.
	EXPECT_FALSE(JobAcceptsBegin(false, 0));
	EXPECT_FALSE(JobAcceptsBegin(true, 0));
}

// ---------------------------------------------------------------- what busy means

TEST(JobDecideStart, StartsWhenNothingIsRunning)
{
	EXPECT_EQ(JobStart::Start, JobDecideStart(false, 3, JobWhenBusy::Refuse));
	EXPECT_EQ(JobStart::Start, JobDecideStart(false, 3, JobWhenBusy::Defer));
}

TEST(JobDecideStart, RefusesOrDefersAccordingToThePolicy)
{
	// The one thing the three workers do not agree on. A resolver asked twice would read the same
	// bytes twice; a download dropped would lose what somebody just asked for.
	EXPECT_EQ(JobStart::Refuse, JobDecideStart(true, 3, JobWhenBusy::Refuse));
	EXPECT_EQ(JobStart::Defer, JobDecideStart(true, 3, JobWhenBusy::Defer));
}

TEST(JobDecideStart, NothingToDoBeatsEvenDeferring)
{
	// Queueing an empty job would have the downloader cancel the run in flight to make room for
	// nothing, which is the one way "defer" could be worse than "refuse".
	EXPECT_EQ(JobStart::Refuse, JobDecideStart(true, 0, JobWhenBusy::Defer));
	EXPECT_EQ(JobStart::Refuse, JobDecideStart(false, 0, JobWhenBusy::Defer));
	EXPECT_EQ(JobStart::Refuse, JobDecideStart(false, 0, JobWhenBusy::Refuse));
}

TEST(JobAcceptsBegin, IsTheRefusePolicyByAnotherName)
{
	// The wrapper and the function it wraps must not drift: every combination, both ways.
	const bool running[] = { false, true };
	const size_t work[] = { 0, 1, 9 };

	for (size_t r = 0; r < 2; ++r)
	{
		for (size_t w = 0; w < 3; ++w)
		{
			const bool expected =
				(JobDecideStart(running[r], work[w], JobWhenBusy::Refuse) == JobStart::Start);

			EXPECT_EQ(expected, JobAcceptsBegin(running[r], work[w]))
				<< "running=" << running[r] << " work=" << work[w];
		}
	}
}

// ---------------------------------------------------------------- the once-per-session scan

TEST(JobAcceptsRescan, RunsTheFirstTimeItIsAsked)
{
	EXPECT_TRUE(JobAcceptsRescan(false, false, false));
}

TEST(JobAcceptsRescan, DoesNotRunAgainOnItsOwn)
{
	// The library's whole point: a list that only has to be built once, asked for from a draw.
	EXPECT_FALSE(JobAcceptsRescan(false, true, false));
}

TEST(JobAcceptsRescan, RunsAgainWhenForced)
{
	EXPECT_TRUE(JobAcceptsRescan(false, true, true));
}

TEST(JobAcceptsRescan, NeverStacksTwoRunsEvenWhenForced)
{
	// Two workers writing the same list is the one outcome forcing must not be able to produce.
	EXPECT_FALSE(JobAcceptsRescan(true, true, true));
	EXPECT_FALSE(JobAcceptsRescan(true, false, true));
	EXPECT_FALSE(JobAcceptsRescan(true, false, false));
}

// ---------------------------------------------------------------- accepting a result

TEST(JobState, TakesAResultThatAnswersTheQuestionBeingAsked)
{
	EXPECT_TRUE(JobAcceptsResult(7, 7));
}

TEST(JobState, DropsAResultTheWorldHasMovedPast)
{
	// The case this exists for: a download landed while the worker was hashing, so what it found is
	// an answer to the question as it stood before, and applying it would show a stale verdict.
	EXPECT_FALSE(JobAcceptsResult(6, 7));
}

TEST(JobState, DropsNothingArrivedRatherThanTreatingItAsAnAnswer)
{
	// Every drain reports "nothing" the same way, and an empty result applied as if it were real
	// would mark every file missing.
	EXPECT_FALSE(JobAcceptsResult(-1, 0));
	EXPECT_FALSE(JobAcceptsResult(-1, -1)) << "not even when the caller has not started one either";
}

TEST(JobState, DoesNotAcceptAnEpochFromTheFuture)
{
	// An epoch only ever goes up, so this cannot happen -- and if it does, the caller stamped its
	// job wrong and quietly accepting it would hide that.
	EXPECT_FALSE(JobAcceptsResult(9, 7));
}

// ---------------------------------------------------------------- epochs

TEST(JobNextEpoch, StaysPutWhileNothingHasChanged)
{
	bool bChanged = true;

	EXPECT_EQ(4, JobNextEpoch(4, Counters(1, 2), Counters(1, 2), bChanged));
	EXPECT_FALSE(bChanged);
}

TEST(JobNextEpoch, AdvancesWhenAnyOneCounterMoves)
{
	bool bChanged = false;

	EXPECT_EQ(5, JobNextEpoch(4, Counters(1, 2), Counters(2, 2), bChanged));
	EXPECT_TRUE(bChanged);

	bChanged = false;
	EXPECT_EQ(5, JobNextEpoch(4, Counters(1, 2), Counters(1, 3), bChanged));
	EXPECT_TRUE(bChanged);
}

TEST(JobNextEpoch, AdvancesByOneNoMatterHowManyMoved)
{
	// The epoch is a stamp, not a total. Two reasons to invalidate at once is still one
	// invalidation, and a jump would be indistinguishable from a missed one.
	bool bChanged = false;

	EXPECT_EQ(5, JobNextEpoch(4, Counters(1, 2), Counters(9, 9), bChanged));
	EXPECT_TRUE(bChanged);
}

TEST(JobNextEpoch, OnlyEverGoesUpEvenWhenACounterGoesBackwards)
{
	// This is why the epoch exists rather than the counters being used directly: a reload can reset
	// one, and a job stamped with a number that has been used before would be believed twice.
	bool bChanged = false;

	EXPECT_EQ(5, JobNextEpoch(4, Counters(7, 7), Counters(0, 0), bChanged));
	EXPECT_TRUE(bChanged);
}

TEST(JobNextEpoch, TreatsAResizeAsAChange)
{
	// The caller itself was rebuilt, so nothing it cached can be trusted to mean what it did.
	bool bChanged = false;
	std::vector<int> one;
	one.push_back(1);

	EXPECT_EQ(5, JobNextEpoch(4, one, Counters(1, 2), bChanged));
	EXPECT_TRUE(bChanged);
}

TEST(JobNextEpoch, AFirstCallWithNothingRememberedIsAChange)
{
	// Nothing has been asked yet, so there is nothing cached to keep -- and the first epoch must not
	// be one a stale result could already be carrying.
	bool bChanged = false;

	EXPECT_EQ(1, JobNextEpoch(0, std::vector<int>(), Counters(0, 0), bChanged));
	EXPECT_TRUE(bChanged);
}

TEST(JobNextEpoch, SurvivesARoundOfEveryCombination)
{
	// The property that matters: repeated calls with unchanged inputs never advance, so a screen
	// drawing sixty times a second does not invalidate its own cache sixty times a second.
	int epoch = 0;
	bool bChanged = false;

	epoch = JobNextEpoch(epoch, std::vector<int>(), Counters(3, 4), bChanged);
	ASSERT_TRUE(bChanged);

	for (int i = 0; i < 60; ++i)
	{
		const int before = epoch;
		epoch = JobNextEpoch(epoch, Counters(3, 4), Counters(3, 4), bChanged);

		EXPECT_EQ(before, epoch);
		EXPECT_FALSE(bChanged);
	}
}
