// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/joinresume_compute.h"

using zx::ComputeResumeAction;
using zx::ResumeAction;

namespace
{
// Named arguments, because ComputeResumeAction(true, false, true, false) at a call site is four
// booleans nobody can read and a test whose failure message means nothing.
ResumeAction Act(bool pending, bool succeeded, bool browserOpen, bool answering)
{
	return ComputeResumeAction(pending, succeeded, browserOpen, answering);
}
} // namespace

TEST(ResumeAction, JoinsImmediatelyWhenTheBrowserIsStillOpen)
{
	// The player is sitting there watching the progress bar. Asking them to confirm the thing they
	// are visibly waiting for is friction and nothing else.
	EXPECT_EQ(ResumeAction::JoinNow, Act(true, true, true, false));
}

TEST(ResumeAction, NotifiesRatherThanJoiningWhenThePlayerWentAway)
{
	// THE BUG THIS EXISTS FOR. Joining here reinitialises the game for the new WAD set out from under
	// whatever they were doing, with no sign beforehand that anything was downloading.
	EXPECT_EQ(ResumeAction::NotifyReady, Act(true, true, false, false));
}

TEST(ResumeAction, AQuestionOnScreenOutranksEverything)
{
	// Including a join that is ready, and including a transfer that failed. The player is mid-way
	// through answering something; their answer is what decides this.
	EXPECT_EQ(ResumeAction::Hold, Act(true, true, true, true));
	EXPECT_EQ(ResumeAction::Hold, Act(true, true, false, true));
	EXPECT_EQ(ResumeAction::Hold, Act(true, false, true, true));
	EXPECT_EQ(ResumeAction::Hold, Act(true, false, false, true));
	EXPECT_EQ(ResumeAction::Hold, Act(false, true, true, true))
		<< "held even with nothing pending -- the answer decides, not us";
}

TEST(ResumeAction, ReportsFailureWhereverThePlayerIs)
{
	// Failure tears nothing down, so there is no reason to sit on it until they wander back.
	EXPECT_EQ(ResumeAction::ReportFailure, Act(true, false, true, false));
	EXPECT_EQ(ResumeAction::ReportFailure, Act(true, false, false, false));
}

TEST(ResumeAction, DoesNothingWithNoJoinWaiting)
{
	// The two false-downloadSucceeded rows are what a CANCELLED transfer looks like: the player
	// answered "stop it", the caller dropped the pending join, and the abort then arrives here
	// looking identical to a failure. It must stay silent -- reporting "couldn't get everything this
	// server needs, see the console" for something they chose on purpose is a diagnosis nobody asked
	// for. See ReleaseJoinResume.

	EXPECT_EQ(ResumeAction::Nothing, Act(false, true, true, false));
	EXPECT_EQ(ResumeAction::Nothing, Act(false, true, false, false));
	EXPECT_EQ(ResumeAction::Nothing, Act(false, false, true, false));
	EXPECT_EQ(ResumeAction::Nothing, Act(false, false, false, false));
}

TEST(ResumeAction, NeverJoinsUnannouncedInAnyCombination)
{
	// The property that matters, over the whole truth table: JoinNow may only ever come back when
	// the browser is open. Every other route to a running join has to pass through the player.
	for (int i = 0; i < 16; ++i)
	{
		const bool pending = (( i & 1 ) != 0 );
		const bool succeeded = (( i & 2 ) != 0 );
		const bool browserOpen = (( i & 4 ) != 0 );
		const bool answering = (( i & 8 ) != 0 );

		if (Act(pending, succeeded, browserOpen, answering) == ResumeAction::JoinNow)
		{
			EXPECT_TRUE(browserOpen) << "case " << i << " joins with the browser closed";
			EXPECT_TRUE(succeeded) << "case " << i << " joins after a failed download";
			EXPECT_FALSE(answering) << "case " << i << " joins through an unanswered prompt";
		}
	}
}

TEST(ResumeAction, EveryCombinationHasAnAnswer)
{
	// No input produces a state nobody wrote a branch for.
	for (int i = 0; i < 16; ++i)
	{
		const ResumeAction action = Act((( i & 1 ) != 0 ), (( i & 2 ) != 0 ),
			(( i & 4 ) != 0 ), (( i & 8 ) != 0 ));

		const bool known = ( action == ResumeAction::Nothing ) || ( action == ResumeAction::Hold ) ||
			( action == ResumeAction::NotifyReady ) || ( action == ResumeAction::JoinNow ) ||
			( action == ResumeAction::ReportFailure );
		EXPECT_TRUE(known) << "case " << i;
	}
}
