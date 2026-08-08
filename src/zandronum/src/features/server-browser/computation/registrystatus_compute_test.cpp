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
