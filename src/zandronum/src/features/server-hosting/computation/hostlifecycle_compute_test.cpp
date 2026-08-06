// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-hosting/computation/hostlifecycle_compute.h"

using zx::ExplainHostFailure;
using zx::HostAcceptsClients;
using zx::HostEvent;
using zx::HostHoldsProcess;
using zx::HostLifecycle;
using zx::HostState;
using zx::HostStateSummary;
using zx::IsHostFinished;
using zx::kReadyTimeoutMs;
using zx::kStopTimeoutMs;
using zx::StepHostLifecycle;
using zx::TickHostLifecycle;
using std::string;

namespace
{
const HostState kStates[] = {
	HostState::Idle, HostState::Starting, HostState::Running,
	HostState::Stopping, HostState::Stopped, HostState::Failed,
};
const HostEvent kEvents[] = {
	HostEvent::Spawned, HostEvent::ReadyObserved, HostEvent::SpawnFailed,
	HostEvent::ChildExited, HostEvent::StopRequested, HostEvent::Timeout,
};
const int kStateCount = 6;
const int kEventCount = 6;

HostLifecycle At(HostState state)
{
	HostLifecycle host;
	host.state = state;
	return host;
}

// Idle -> Starting -> Running, the way it happens when nothing goes wrong.
HostLifecycle Healthy()
{
	HostLifecycle host;
	host = StepHostLifecycle(host, HostEvent::Spawned, "");
	host = StepHostLifecycle(host, HostEvent::ReadyObserved, "");
	return host;
}
} // namespace

// ---------------------------------------------------------------- the happy path

TEST(HostLifecycle, StartsIdle)
{
	const HostLifecycle fresh;

	EXPECT_EQ(HostState::Idle, fresh.state);
	EXPECT_TRUE(fresh.reason.empty());
	EXPECT_EQ(0, fresh.elapsedMs);
}

TEST(HostLifecycle, SpawningIsNotYetRunning)
{
	// [rc4l] The distinction the whole unit exists for. A process that exists is not a server that
	// listens, and the gap between them is where every interesting failure lives.
	const HostLifecycle host = StepHostLifecycle(HostLifecycle(), HostEvent::Spawned, "");

	EXPECT_EQ(HostState::Starting, host.state);
	EXPECT_FALSE(HostAcceptsClients(host.state));
	EXPECT_TRUE(HostHoldsProcess(host.state));
}

TEST(HostLifecycle, ReadinessIsWhatOpensTheDoor)
{
	const HostLifecycle host = Healthy();

	EXPECT_EQ(HostState::Running, host.state);
	EXPECT_TRUE(HostAcceptsClients(host.state));
}

TEST(HostLifecycle, StoppingThenExitingIsASuccess)
{
	HostLifecycle host = Healthy();
	host = StepHostLifecycle(host, HostEvent::StopRequested, "");
	EXPECT_EQ(HostState::Stopping, host.state);

	host = StepHostLifecycle(host, HostEvent::ChildExited, "");
	EXPECT_EQ(HostState::Stopped, host.state);
	EXPECT_TRUE(host.reason.empty());
}

// ---------------------------------------------------------------- failure

TEST(HostLifecycle, DyingUnaskedIsAFailureWithAReason)
{
	HostLifecycle host = Healthy();
	host = StepHostLifecycle(host, HostEvent::ChildExited, "That port is in use.");

	EXPECT_EQ(HostState::Failed, host.state);
	EXPECT_EQ("That port is in use.", host.reason);
}

TEST(HostLifecycle, AFailureAlwaysCarriesSomethingToShow)
{
	// A Failed state with an empty reason is a dialog that says nothing, which is worse than the
	// generic sentence it would have replaced.
	const HostLifecycle exited = StepHostLifecycle(Healthy(), HostEvent::ChildExited, "");
	EXPECT_FALSE(exited.reason.empty());

	const HostLifecycle nospawn = StepHostLifecycle(HostLifecycle(), HostEvent::SpawnFailed, "");
	EXPECT_EQ(HostState::Failed, nospawn.state);
	EXPECT_FALSE(nospawn.reason.empty());
}

TEST(HostLifecycle, SpawnFailureIsFatalFromAnywhereItCanHappen)
{
	const HostLifecycle host = StepHostLifecycle(At(HostState::Starting),
		HostEvent::SpawnFailed, "No such executable.");

	EXPECT_EQ(HostState::Failed, host.state);
	EXPECT_EQ("No such executable.", host.reason);
}

TEST(HostLifecycle, DyingDuringStartupIsAFailureNotAStop)
{
	HostLifecycle host = StepHostLifecycle(HostLifecycle(), HostEvent::Spawned, "");
	host = StepHostLifecycle(host, HostEvent::ChildExited, "Could not find the IWAD.");

	EXPECT_EQ(HostState::Failed, host.state);
	EXPECT_EQ("Could not find the IWAD.", host.reason);
}

// ---------------------------------------------------------------- time

TEST(HostLifecycle, AStartThatNeverFinishesEventuallyFails)
{
	HostLifecycle host = StepHostLifecycle(HostLifecycle(), HostEvent::Spawned, "");
	host = TickHostLifecycle(host, kReadyTimeoutMs);

	EXPECT_EQ(HostState::Failed, host.state);
	EXPECT_FALSE(host.reason.empty());
}

TEST(HostLifecycle, AStartIsGivenRealTimeFirst)
{
	// A cold disk loading a large WAD set is slow. Cutting a healthy start short would produce a
	// failure message that is simply untrue.
	HostLifecycle host = StepHostLifecycle(HostLifecycle(), HostEvent::Spawned, "");
	host = TickHostLifecycle(host, kReadyTimeoutMs - 1);

	EXPECT_EQ(HostState::Starting, host.state);
}

TEST(HostLifecycle, AStopThatHangsIsStillAStop)
{
	// The caller kills the process on this same signal, so reporting anything else would leave the
	// UI waiting on something that is already gone.
	HostLifecycle host = StepHostLifecycle(Healthy(), HostEvent::StopRequested, "");
	host = TickHostLifecycle(host, kStopTimeoutMs);

	EXPECT_EQ(HostState::Stopped, host.state);
}

TEST(HostLifecycle, RunningIsNotOnAClock)
{
	// A server that has been up for hours is not a server that has timed out.
	HostLifecycle host = Healthy();
	for (int i = 0; i < 100; ++i)
		host = TickHostLifecycle(host, 60000);

	EXPECT_EQ(HostState::Running, host.state);
}

TEST(HostLifecycle, TimeOnlyMovesForward)
{
	HostLifecycle host = StepHostLifecycle(HostLifecycle(), HostEvent::Spawned, "");
	const HostLifecycle back = TickHostLifecycle(host, -5000);

	EXPECT_EQ(0, back.elapsedMs);
	EXPECT_EQ(HostState::Starting, back.state);
}

TEST(HostLifecycle, EnteringAStateRestartsItsClock)
{
	HostLifecycle host = StepHostLifecycle(HostLifecycle(), HostEvent::Spawned, "");
	host = TickHostLifecycle(host, kReadyTimeoutMs - 1);
	host = StepHostLifecycle(host, HostEvent::ReadyObserved, "");

	// Otherwise a slow start would have the stop timeout already expired the moment it was requested.
	EXPECT_EQ(0, host.elapsedMs);
}

// ---------------------------------------------------------------- the one-way rule

TEST(HostLifecycle, NothingResurrectsAFinishedHost)
{
	// [rc4l] Events keep arriving after a host ends -- a pipe drains, an exit code lands after we
	// already gave up. None may put the client back on a pid that no longer exists.
	const HostState terminal[] = { HostState::Stopped, HostState::Failed };

	for (int t = 0; t < 2; ++t)
	{
		for (int e = 0; e < kEventCount; ++e)
		{
			const HostLifecycle after = StepHostLifecycle(At(terminal[t]), kEvents[e], "anything");
			EXPECT_EQ(terminal[t], after.state) << t << "," << e;
		}
	}
}

TEST(HostLifecycle, ReadinessCannotArriveOutOfNowhere)
{
	// Only a Starting host can become Running. A stray readiness line from a process we never
	// spawned must not open the door.
	EXPECT_EQ(HostState::Idle, StepHostLifecycle(At(HostState::Idle), HostEvent::ReadyObserved, "").state);
	EXPECT_EQ(HostState::Stopping,
		StepHostLifecycle(At(HostState::Stopping), HostEvent::ReadyObserved, "").state);
}

TEST(HostLifecycle, SpawningTwiceDoesNotRestartTheClock)
{
	const HostLifecycle running = Healthy();
	EXPECT_EQ(HostState::Running, StepHostLifecycle(running, HostEvent::Spawned, "").state);
}

TEST(HostLifecycle, StoppingWhatWasNeverStartedIsAlreadyDone)
{
	EXPECT_EQ(HostState::Stopped,
		StepHostLifecycle(At(HostState::Idle), HostEvent::StopRequested, "").state);
}

TEST(HostLifecycle, AnExitWeNeverHadIsIgnored)
{
	EXPECT_EQ(HostState::Idle, StepHostLifecycle(At(HostState::Idle), HostEvent::ChildExited, "").state);
}

TEST(HostLifecycle, StoppingTwiceIsStillStopping)
{
	HostLifecycle host = StepHostLifecycle(Healthy(), HostEvent::StopRequested, "");
	EXPECT_EQ(HostState::Stopping, StepHostLifecycle(host, HostEvent::StopRequested, "").state);
}

TEST(HostLifecycle, IdleNeverTimesOut)
{
	EXPECT_EQ(HostState::Idle, StepHostLifecycle(At(HostState::Idle), HostEvent::Timeout, "").state);
	EXPECT_EQ(HostState::Idle, TickHostLifecycle(At(HostState::Idle), 10 * kReadyTimeoutMs).state);
}

TEST(HostLifecycle, RunningIgnoresATimeoutItDidNotAskFor)
{
	EXPECT_EQ(HostState::Running, StepHostLifecycle(At(HostState::Running), HostEvent::Timeout, "").state);
}

TEST(HostLifecycle, EveryStateAndEventPairIsSurvivable)
{
	// Swept because these events come from a process we do not control. A duplicate exit
	// notification, an out-of-order pipe line -- none may produce a state nobody wrote down.
	for (int s = 0; s < kStateCount; ++s)
	{
		for (int e = 0; e < kEventCount; ++e)
		{
			const HostLifecycle after = StepHostLifecycle(At(kStates[s]), kEvents[e], "detail");

			bool known = false;
			for (int k = 0; k < kStateCount; ++k)
				known = known || (after.state == kStates[k]);

			EXPECT_TRUE(known) << s << "," << e;

			// A failure this machine CAUSES must always be able to explain itself. A Failed host
			// handed in already reasonless is passed straight back out, which is right -- inventing
			// a reason for a failure it knows nothing about would be worse than the silence.
			if ((after.state == HostState::Failed) && (kStates[s] != HostState::Failed))
				EXPECT_FALSE(after.reason.empty()) << s << "," << e;
		}
	}
}

// ---------------------------------------------------------------- the predicates

TEST(HostPredicates, HoldingAProcessIncludesStopping)
{
	// [rc4l] The one that matters for teardown. A Stopping host still owns a pid, and teardown that
	// skipped it would leave the orphan this unit exists to prevent.
	EXPECT_TRUE(HostHoldsProcess(HostState::Starting));
	EXPECT_TRUE(HostHoldsProcess(HostState::Running));
	EXPECT_TRUE(HostHoldsProcess(HostState::Stopping));

	EXPECT_FALSE(HostHoldsProcess(HostState::Idle));
	EXPECT_FALSE(HostHoldsProcess(HostState::Stopped));
	EXPECT_FALSE(HostHoldsProcess(HostState::Failed));
}

TEST(HostPredicates, OnlyRunningTakesClients)
{
	for (int s = 0; s < kStateCount; ++s)
		EXPECT_EQ(kStates[s] == HostState::Running, HostAcceptsClients(kStates[s])) << s;
}

TEST(HostPredicates, FinishedMeansNothingIsLeftToWaitFor)
{
	EXPECT_TRUE(IsHostFinished(HostState::Idle));
	EXPECT_TRUE(IsHostFinished(HostState::Stopped));
	EXPECT_TRUE(IsHostFinished(HostState::Failed));

	EXPECT_FALSE(IsHostFinished(HostState::Starting));
	EXPECT_FALSE(IsHostFinished(HostState::Running));
	EXPECT_FALSE(IsHostFinished(HostState::Stopping));
}

TEST(HostPredicates, EveryStateHasSomethingToSay)
{
	for (int s = 0; s < kStateCount; ++s)
	{
		const char *text = HostStateSummary(kStates[s]);
		ASSERT_NE(static_cast<const char *>(NULL), text) << s;
		EXPECT_NE('\0', text[0]) << s;
	}

	EXPECT_STRNE(HostStateSummary(HostState::Running), HostStateSummary(HostState::Failed));
}

TEST(HostPredicates, ASummaryExistsForAValueNobodyDefined)
{
	// Defensive, and reachable: the enum crosses a boundary as an int in the UI layer.
	const char *text = HostStateSummary(static_cast<HostState>(99));
	ASSERT_NE(static_cast<const char *>(NULL), text);
	EXPECT_NE('\0', text[0]);
}

// ---------------------------------------------------------------- explaining a death

TEST(HostFailure, NamesTheThingThePlayerCanChange)
{
	// "Could not bind to port" is half an answer to someone who never picked a port.
	const string bind = ExplainHostFailure("Error: Could not bind to port 10666", 1);
	EXPECT_NE(string::npos, bind.find("already in use"));
	EXPECT_NE(string::npos, bind.find("different port"));
}

TEST(HostFailure, RecognisesTheCommonCauses)
{
	EXPECT_NE(string::npos,
		ExplainHostFailure("Address already in use", 1).find("already in use"));
	EXPECT_NE(string::npos,
		ExplainHostFailure("EADDRINUSE", 1).find("already in use"));
	EXPECT_NE(string::npos,
		ExplainHostFailure("Could not find IWAD file", 1).find("game data"));
	EXPECT_NE(string::npos,
		ExplainHostFailure("Cannot find a game IWAD", 1).find("game data"));
	EXPECT_NE(string::npos,
		ExplainHostFailure("missing game data", 1).find("game data"));
	EXPECT_NE(string::npos,
		ExplainHostFailure("Could not open maps.wad", 1).find("could not open"));
	EXPECT_NE(string::npos,
		ExplainHostFailure("No such file or directory", 1).find("could not open"));
	EXPECT_NE(string::npos,
		ExplainHostFailure("could not find maps.wad", 1).find("could not open"));
	EXPECT_NE(string::npos,
		ExplainHostFailure("Unknown map MAP99", 1).find("not in the files"));
	EXPECT_NE(string::npos,
		ExplainHostFailure("MAP99 is not a valid map", 1).find("not in the files"));
}

TEST(HostFailure, MatchesWhateverCaseTheEngineUsed)
{
	// We are matching prose, not a protocol, and the engine's messages are not written to one case.
	EXPECT_NE(string::npos, ExplainHostFailure("COULD NOT BIND", 1).find("already in use"));
	EXPECT_NE(string::npos, ExplainHostFailure("could not bind", 1).find("already in use"));
}

TEST(HostFailure, PassesThroughSomethingItDoesNotRecognise)
{
	// [rc4l] A strange message is still better evidence than a generic one. The last line wins --
	// the startup banner above it is not an explanation.
	const string out = ExplainHostFailure(
		"ZandroX 0.2.0\nLoading things\nSomething went badly wrong\n", 1);

	EXPECT_EQ("Something went badly wrong", out);
}

TEST(HostFailure, PassesThroughASingleLineWithNoNewlines)
{
	EXPECT_EQ("weird internal thing", ExplainHostFailure("weird internal thing", 1));
}

TEST(HostFailure, SaysSomethingEvenWithNothingToGoOn)
{
	const string silent = ExplainHostFailure("", 0);
	EXPECT_FALSE(silent.empty());

	const string coded = ExplainHostFailure("", 3);
	EXPECT_FALSE(coded.empty());
	EXPECT_NE(string::npos, coded.find("3"));
}

TEST(HostFailure, WhitespaceOnlyOutputCountsAsNothingToGoOn)
{
	const string out = ExplainHostFailure("   \n\t\r\n  ", 0);

	EXPECT_FALSE(out.empty());
	EXPECT_NE(string::npos, out.find("without saying why"));
}

TEST(HostFailure, ANegativeExitCodeStillReads)
{
	// Windows returns these for a process killed by an exception.
	EXPECT_NE(string::npos, ExplainHostFailure("", -1073741819).find("-1073741819"));
}
