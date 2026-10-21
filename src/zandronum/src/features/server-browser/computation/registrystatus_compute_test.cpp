// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/registrystatus_compute.h"

#include <string>

using zx::RegistryStatus;
using zx::RegistryStatusCode;
using zx::RegistryStatusIsFinished;
using zx::RegistryStatusText;
using zx::RegistryTone;
using zx::RegistryToneFor;
using zx::RegistryTooltip;

namespace
{

const RegistryStatus kAll[] = {
	RegistryStatus::Pending,
	RegistryStatus::Ok,
	RegistryStatus::LookupFailed,
	RegistryStatus::NoAnswer,
	RegistryStatus::Throttled,
	RegistryStatus::Banned,
	RegistryStatus::Version,
};

const size_t kAllCount = sizeof(kAll) / sizeof(kAll[0]);

} // namespace

// ---------------------------------------------------------------- the distinction that matters

TEST(RegistryStatus, NothingSentAndNothingBackAreDifferentAnswers)
{
	// [rc4l] The pair this whole unit exists for. A name that will not resolve is wrong in the
	// setting; an address that never replies is wrong on the network, and telling someone to check
	// their spelling when the host is simply down sends them to the wrong place.
	EXPECT_STRNE(RegistryStatusCode(RegistryStatus::LookupFailed),
		RegistryStatusCode(RegistryStatus::NoAnswer));

	EXPECT_STRNE(RegistryStatusText(RegistryStatus::LookupFailed),
		RegistryStatusText(RegistryStatus::NoAnswer));
}

TEST(RegistryStatus, BeingThrottledIsNotColouredLikeBeingBroken)
{
	// It clears itself in a few seconds. Painting it the same red as a ban would have people
	// reconfiguring a network that was never wrong, which is the mistake the reach probe already
	// avoids by keeping Failed apart from Unreachable.
	EXPECT_EQ(RegistryTone::Warn, RegistryToneFor(RegistryStatus::Throttled));
	EXPECT_EQ(RegistryTone::Bad, RegistryToneFor(RegistryStatus::Banned));
}

// ---------------------------------------------------------------- tone

TEST(RegistryStatus, WaitingIsItsOwnToneAndNotAVerdict)
{
	EXPECT_EQ(RegistryTone::Waiting, RegistryToneFor(RegistryStatus::Pending));
	EXPECT_FALSE(RegistryStatusIsFinished(RegistryStatus::Pending));
}

TEST(RegistryStatus, OnlyAnAnsweredListIsGood)
{
	EXPECT_EQ(RegistryTone::Good, RegistryToneFor(RegistryStatus::Ok));

	for (size_t i = 0; i < kAllCount; ++i)
	{
		if (kAll[i] == RegistryStatus::Ok)
			continue;

		EXPECT_NE(RegistryTone::Good, RegistryToneFor(kAll[i]))
			<< "not good: " << RegistryStatusCode(kAll[i]);
	}
}

TEST(RegistryStatus, EveryFailureThatWillNotClearItselfReadsAsBad)
{
	EXPECT_EQ(RegistryTone::Bad, RegistryToneFor(RegistryStatus::LookupFailed));
	EXPECT_EQ(RegistryTone::Bad, RegistryToneFor(RegistryStatus::NoAnswer));
	EXPECT_EQ(RegistryTone::Bad, RegistryToneFor(RegistryStatus::Banned));
	EXPECT_EQ(RegistryTone::Bad, RegistryToneFor(RegistryStatus::Version));
}

TEST(RegistryStatus, EverythingExceptWaitingIsAFinishedAnswer)
{
	for (size_t i = 0; i < kAllCount; ++i)
	{
		if (kAll[i] == RegistryStatus::Pending)
			continue;

		EXPECT_TRUE(RegistryStatusIsFinished(kAll[i]))
			<< "should be finished: " << RegistryStatusCode(kAll[i]);
	}
}

// ---------------------------------------------------------------- the codes themselves

TEST(RegistryStatus, EveryStatusHasItsOwnCodeAndItsOwnSentence)
{
	// A duplicate code would make two different outcomes indistinguishable in a log, which is the
	// failure this unit is meant to end rather than repeat.
	for (size_t i = 0; i < kAllCount; ++i)
	{
		const std::string code = RegistryStatusCode(kAll[i]);
		const std::string text = RegistryStatusText(kAll[i]);

		EXPECT_FALSE(code.empty());
		EXPECT_FALSE(text.empty());
		EXPECT_EQ(0u, code.find("REG_")) << code << " should carry the prefix";

		for (size_t j = i + 1; j < kAllCount; ++j)
		{
			EXPECT_NE(code, std::string(RegistryStatusCode(kAll[j])));
			EXPECT_NE(text, std::string(RegistryStatusText(kAll[j])));
		}
	}
}

TEST(RegistryStatus, TheCodesAreTheOnesWeActuallyUse)
{
	EXPECT_STREQ("REG_PENDING", RegistryStatusCode(RegistryStatus::Pending));
	EXPECT_STREQ("REG_OK", RegistryStatusCode(RegistryStatus::Ok));
	EXPECT_STREQ("REG_LOOKUP_FAILED", RegistryStatusCode(RegistryStatus::LookupFailed));
	EXPECT_STREQ("REG_NO_ANSWER", RegistryStatusCode(RegistryStatus::NoAnswer));
	EXPECT_STREQ("REG_THROTTLED", RegistryStatusCode(RegistryStatus::Throttled));
	EXPECT_STREQ("REG_BANNED", RegistryStatusCode(RegistryStatus::Banned));
	EXPECT_STREQ("REG_VERSION", RegistryStatusCode(RegistryStatus::Version));
}

// ---------------------------------------------------------------- the hover text

TEST(RegistryStatus, TheTooltipNamesTheRegistryBeforeSayingAnythingAboutIt)
{
	// With several configured, "which one is this" is the question the pointer is asking.
	const std::string tip = RegistryTooltip("registry.cantstopscrolling.net", 15300, RegistryStatus::Ok);

	EXPECT_EQ("registry.cantstopscrolling.net:15300\nREG_OK\nGot a valid response!", tip)
		<< "address, then code, then meaning, one per line";
}

TEST(RegistryStatus, APortOfZeroIsLeftOffRatherThanPrinted)
{
	// What a failed lookup has: there was never an address, so there was never a port.
	const std::string tip = RegistryTooltip("typo.example.nett", 0, RegistryStatus::LookupFailed);

	EXPECT_EQ(std::string::npos, tip.find(":0"));
	EXPECT_EQ(0u, tip.find("typo.example.nett\n"));
}

TEST(RegistryStatus, AMultiDigitPortSurvivesIntact)
{
	// The number is assembled a digit at a time, so it is worth proving it comes out in order.
	EXPECT_NE(std::string::npos, RegistryTooltip("h", 15300, RegistryStatus::Ok).find("h:15300"));
	EXPECT_NE(std::string::npos, RegistryTooltip("h", 7, RegistryStatus::Ok).find("h:7"));
	EXPECT_NE(std::string::npos, RegistryTooltip("h", 65535, RegistryStatus::Ok).find("h:65535"));
}

TEST(RegistryStatus, ARegistryWithNoNameStillSaysSomething)
{
	const std::string tip = RegistryTooltip("", 15300, RegistryStatus::NoAnswer);

	EXPECT_FALSE(tip.empty());
	EXPECT_EQ(0u, tip.find("(unnamed registry):15300"));
}

TEST(RegistryStatus, EveryStatusProducesAUsableTooltip)
{
	for (size_t i = 0; i < kAllCount; ++i)
	{
		const std::string tip = RegistryTooltip("host", 15300, kAll[i]);

		EXPECT_NE(std::string::npos, tip.find("host:15300"));
		EXPECT_NE(std::string::npos, tip.find(RegistryStatusCode(kAll[i])));
		EXPECT_NE(std::string::npos, tip.find(RegistryStatusText(kAll[i])));
	}
}

// ------------------------------------------------- throttled decays back

namespace
{
const int kThrottleClearMs = 4000;
}

TEST( RegistryStatus, ThrottledStopsMeaningAnythingAfterItsWindow )
{
	// [rc4l] The bug this pins: nothing ever cleared Throttled, so one REQUESTIGNORED left the
	// status bar orange long after the registry had gone back to answering.
	EXPECT_EQ( zx::RegistryStatus::Pending,
		zx::AgeRegistryStatus( zx::RegistryStatus::Throttled, kThrottleClearMs, kThrottleClearMs ));
}

TEST( RegistryStatus, ThrottledHoldsInsideItsWindow )
{
	EXPECT_EQ( zx::RegistryStatus::Throttled,
		zx::AgeRegistryStatus( zx::RegistryStatus::Throttled, kThrottleClearMs - 1, kThrottleClearMs ));
}

TEST( RegistryStatus, ThrottledDecaysToPendingNotToOk )
{
	// "They were busy a moment ago" is not evidence that they are answering now.
	const zx::RegistryStatus aged =
		zx::AgeRegistryStatus( zx::RegistryStatus::Throttled, 60000, kThrottleClearMs );

	EXPECT_NE( zx::RegistryStatus::Ok, aged );
	EXPECT_EQ( zx::RegistryStatus::Pending, aged );
}

TEST( RegistryStatus, FinishedVerdictsAreNotWornAwayByTime )
{
	// Banned, wrong version and no-answer are facts about a conversation that ended. Waiting does
	// not make them less true, and decaying them would quietly hide a real refusal.
	const zx::RegistryStatus keep[] = {
		zx::RegistryStatus::Ok,
		zx::RegistryStatus::Banned,
		zx::RegistryStatus::Version,
		zx::RegistryStatus::NoAnswer,
		zx::RegistryStatus::Pending,
	};

	for ( unsigned int i = 0; i < ( sizeof keep / sizeof keep[0] ); ++i )
	{
		EXPECT_EQ( keep[i], zx::AgeRegistryStatus( keep[i], 999999, kThrottleClearMs ))
			<< "status index " << i;
	}
}

TEST( RegistryStatus, ANonPositiveWindowPinsTheStatus )
{
	EXPECT_EQ( zx::RegistryStatus::Throttled,
		zx::AgeRegistryStatus( zx::RegistryStatus::Throttled, 999999, 0 ));
}

TEST( RegistryStatus, ABackwardsClockDoesNotClearIt )
{
	EXPECT_EQ( zx::RegistryStatus::Throttled,
		zx::AgeRegistryStatus( zx::RegistryStatus::Throttled, -1, kThrottleClearMs ));
}

// ---------------------------------------------------------------- what a new answer replaces

using zx::ComputeRecordedStatus;

// The whole reason this exists: the second query of a pair gets refused, and used to undo the first.
TEST(RegistryStatus, AThrottleDoesNotUnseatAnOk)
{
	EXPECT_EQ(RegistryStatus::Ok,
		ComputeRecordedStatus(RegistryStatus::Ok, RegistryStatus::Throttled));
}

// A registry that can refuse us is a registry that is up, which is better news than silence.
TEST(RegistryStatus, AThrottleReplacesAFailure)
{
	EXPECT_EQ(RegistryStatus::Throttled,
		ComputeRecordedStatus(RegistryStatus::NoAnswer, RegistryStatus::Throttled));
	EXPECT_EQ(RegistryStatus::Throttled,
		ComputeRecordedStatus(RegistryStatus::LookupFailed, RegistryStatus::Throttled));
	EXPECT_EQ(RegistryStatus::Throttled,
		ComputeRecordedStatus(RegistryStatus::Pending, RegistryStatus::Throttled));
	EXPECT_EQ(RegistryStatus::Throttled,
		ComputeRecordedStatus(RegistryStatus::Throttled, RegistryStatus::Throttled));
}

// Everything else is simply the newer fact, including bad news arriving on top of good.
TEST(RegistryStatus, AnyOtherAnswerIsTheOneThatCounts)
{
	EXPECT_EQ(RegistryStatus::NoAnswer,
		ComputeRecordedStatus(RegistryStatus::Ok, RegistryStatus::NoAnswer));
	EXPECT_EQ(RegistryStatus::Banned,
		ComputeRecordedStatus(RegistryStatus::Ok, RegistryStatus::Banned));
	EXPECT_EQ(RegistryStatus::Version,
		ComputeRecordedStatus(RegistryStatus::Throttled, RegistryStatus::Version));
	EXPECT_EQ(RegistryStatus::Ok,
		ComputeRecordedStatus(RegistryStatus::NoAnswer, RegistryStatus::Ok));
	EXPECT_EQ(RegistryStatus::Ok,
		ComputeRecordedStatus(RegistryStatus::Ok, RegistryStatus::Ok));
}

// [rc4l] Pending is the absence of news, so it never speaks for the registry.
TEST(RegistryStatusTest, PendingDefersToWhatWeLastHeard)
{
	using namespace zx;
	EXPECT_EQ(RegistryStatus::Ok,
		ComputeKnownStatus(RegistryStatus::Pending, RegistryStatus::Ok));
	EXPECT_EQ(RegistryStatus::NoAnswer,
		ComputeKnownStatus(RegistryStatus::Pending, RegistryStatus::NoAnswer));
	EXPECT_EQ(RegistryStatus::Pending,
		ComputeKnownStatus(RegistryStatus::Pending, RegistryStatus::Pending));
}

// A real verdict speaks for itself; the older one is not consulted.
TEST(RegistryStatusTest, AVerdictOutranksTheOneBeforeIt)
{
	using namespace zx;
	EXPECT_EQ(RegistryStatus::Throttled,
		ComputeKnownStatus(RegistryStatus::Throttled, RegistryStatus::Ok));
	EXPECT_EQ(RegistryStatus::Ok,
		ComputeKnownStatus(RegistryStatus::Ok, RegistryStatus::NoAnswer));
}

// [rc4l] The whole sequence the player saw: an answer, then asking again, then a refusal.
TEST(RegistryStatusTest, ARefusalAfterAnAnswerKeepsTheAnswer)
{
	using namespace zx;
	const RegistryStatus prior = RegistryStatus::Ok;			// the background query answered
	const RegistryStatus current = RegistryStatus::Pending;		// opening the browser asked again
	EXPECT_EQ(RegistryStatus::Ok, ComputeRecordedStatus(
		ComputeKnownStatus(current, prior), RegistryStatus::Throttled));
}
