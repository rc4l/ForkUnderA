// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-hosting/computation/hoststatus_compute.h"

#include <string>

using zx::HostStatus;
using zx::HostStatusCode;
using zx::HostStatusText;
using zx::HostStatusTooltip;
using zx::HostTone;
using zx::HostToneFor;

namespace
{

bool Has(const std::string &haystack, const char *needle)
{
	return haystack.find(needle) != std::string::npos;
}

int LineCount(const std::string &s)
{
	int n = 1;
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '\n')
			++n;
	}
	return n;
}

} // namespace

// ---------------------------------------------------------------- tones

TEST(HostStatusTone, OnlyAStrangerArrivingIsGood)
{
	// [rc4l] Good is said about one thing only, and it is the one thing we observed rather than
	// inferred. A router agreeing is not this, and neither is a port looking open from in here.
	EXPECT_EQ(HostTone::Good, HostToneFor(HostStatus::Open));

	EXPECT_NE(HostTone::Good, HostToneFor(HostStatus::Checking));
	EXPECT_NE(HostTone::Good, HostToneFor(HostStatus::LanOnly));
	EXPECT_NE(HostTone::Good, HostToneFor(HostStatus::NoReply));
}

TEST(HostStatusTone, LanOnlyIsNotAFailure)
{
	// The player picked it on the visibility row. Colouring it like a fault would report their own
	// choice back to them as a problem.
	EXPECT_EQ(HostTone::Info, HostToneFor(HostStatus::LanOnly));
	EXPECT_NE(HostTone::Bad, HostToneFor(HostStatus::LanOnly));
}

TEST(HostStatusTone, WaitingIsNotAVerdict)
{
	EXPECT_EQ(HostTone::Waiting, HostToneFor(HostStatus::Checking));
}

TEST(HostStatusTone, NothingArrivingIsTheOnlyBadOne)
{
	EXPECT_EQ(HostTone::Bad, HostToneFor(HostStatus::NoReply));
}

// ---------------------------------------------------------------- codes

TEST(HostStatusCodes, EveryStatusHasItsOwnCodeAndWords)
{
	const HostStatus all[] = {
		HostStatus::LanOnly, HostStatus::Checking, HostStatus::Open, HostStatus::NoReply,
	};

	for (size_t i = 0; i < 4; ++i)
	{
		EXPECT_STRNE("", HostStatusCode(all[i]));
		EXPECT_STRNE("", HostStatusText(all[i]));

		// A code shared between two states is a code that identifies nothing.
		for (size_t j = i + 1; j < 4; ++j)
			EXPECT_STRNE(HostStatusCode(all[i]), HostStatusCode(all[j]));
	}
}

TEST(HostStatusCodes, TheCodesAreTheOnesTheUiPromises)
{
	// Pinned because these end up in screenshots and bug reports; renaming one silently would make
	// every older report unmatchable.
	EXPECT_STREQ("HOST_OPEN", HostStatusCode(HostStatus::Open));
	EXPECT_STREQ("HOST_CHECKING", HostStatusCode(HostStatus::Checking));
	EXPECT_STREQ("HOST_LAN_ONLY", HostStatusCode(HostStatus::LanOnly));
	EXPECT_STREQ("HOST_NO_REPLY", HostStatusCode(HostStatus::NoReply));
}

TEST(HostStatusCodes, TheFailureDoesNotBlameTheRouter)
{
	// [rc4l] An unforwarded port, a router that ignored us, carrier-grade NAT and a registry that was
	// briefly down are indistinguishable from inside this process, and three of those four are not
	// the player's router. The name says what we saw.
	const std::string text = HostStatusText(HostStatus::NoReply);
	EXPECT_TRUE(Has(text, "reached"));
	EXPECT_FALSE(Has(text, "closed"));
	EXPECT_FALSE(Has(text, "blocked"));
}

// ---------------------------------------------------------------- the tooltip

TEST(HostStatusTooltip, LeadsWithTheCodeThenTheMeaning)
{
	const std::string tip = HostStatusTooltip(HostStatus::Open, 10666, "");

	EXPECT_EQ(0u, tip.find("HOST_OPEN\n")) << "the code is the first line";
	EXPECT_TRUE(Has(tip, HostStatusText(HostStatus::Open)));
}

TEST(HostStatusTooltip, TellsYouWhatToForwardOnlyWhenItFailed)
{
	const std::string bad = HostStatusTooltip(HostStatus::NoReply, 10666, "");
	EXPECT_TRUE(Has(bad, "10666"));
	EXPECT_TRUE(Has(bad, "TCP and UDP")) << "both, or downloads break while the game works";

	// [rc4l] Not while we are still waiting to find out. Instructions given before there is a problem
	// turn a check into a chore.
	for (int i = 0; i < 3; ++i)
	{
		const HostStatus quiet[] = { HostStatus::Checking, HostStatus::Open, HostStatus::LanOnly };
		const std::string tip = HostStatusTooltip(quiet[i], 10666, "");
		EXPECT_FALSE(Has(tip, "forward")) << HostStatusCode(quiet[i]);
		EXPECT_FALSE(Has(tip, "10666")) << HostStatusCode(quiet[i]);
	}
}

TEST(HostStatusTooltip, SaysItStillWorksLocallyBeforeAskingForAnything)
{
	// The reassurance matters: the server is not broken, it is just not visible from outside.
	const std::string tip = HostStatusTooltip(HostStatus::NoReply, 10666, "");
	EXPECT_LT(tip.find("still works"), tip.find("forward"));
}

TEST(HostStatusTooltip, AnUnknownPortIsLeftOutRatherThanPrintedAsZero)
{
	const std::string tip = HostStatusTooltip(HostStatus::NoReply, 0, "");

	EXPECT_TRUE(Has(tip, "still works"));
	EXPECT_FALSE(Has(tip, "forward")) << "no port to name means no instruction to give";
	EXPECT_FALSE(Has(tip, " 0 "));
}

TEST(HostStatusTooltip, TheRouterLineGoesLastAndOnlyWhenThereIsOne)
{
	const std::string with = HostStatusTooltip(HostStatus::NoReply, 10666, "Could not ask your router");
	EXPECT_TRUE(Has(with, "Could not ask your router"));
	EXPECT_GT(with.find("Could not ask your router"), with.find("forward"))
		<< "it explains the verdict rather than being it";

	const std::string without = HostStatusTooltip(HostStatus::NoReply, 10666, "");
	EXPECT_EQ(LineCount(with), LineCount(without) + 1);
}

TEST(HostStatusTooltip, TheRouterLineIsCarriedByEveryStatus)
{
	// It is a fact about what we attempted, not about the verdict, so it is not suppressed by a good
	// one -- a player who wants to know whether we touched their router should not have to fail first.
	const std::string tip = HostStatusTooltip(HostStatus::Open, 10666, "Asked your router to open the port");
	EXPECT_TRUE(Has(tip, "Asked your router"));
}
