// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-hosting/computation/hostport_compute.h"
#include "features/server-hosting/computation/reachprobe_compute.h"

using namespace zx;

TEST(HostPort, BeforeHostingItIsTheFieldYouAreEditing)
{
	// Nothing is running, so the question is "will the port I am about to use work", and the answer
	// has to follow the field as it is typed.
	EXPECT_EQ(10666, PortToCheck(0, 10666));
	EXPECT_EQ(27015, PortToCheck(0, 27015));
}

TEST(HostPort, OnceAServerExistsItIsThePortTheServerActuallyHolds)
{
	// This is the whole bug. A forwarded 10666 with the server on 10670 used to report "reachable",
	// which is true of the port that was tested and useless to the player.
	EXPECT_EQ(10670, PortToCheck(10670, 10666));
}

TEST(HostPort, TheRunningPortWinsEvenWhenItMatches)
{
	EXPECT_EQ(10666, PortToCheck(10666, 10666));
}

TEST(HostPort, ANegativeOrZeroRunningPortMeansNothingIsHeld)
{
	EXPECT_EQ(10666, PortToCheck(0, 10666));
	EXPECT_EQ(10666, PortToCheck(-1, 10666));
}

TEST(HostPort, DriftIsWorthSayingOutLoud)
{
	// Landing on a port nobody forwarded is the most likely reason a working setup stops working,
	// and it cannot be seen from inside the game.
	EXPECT_TRUE(PortDriftNeedsWarning(10670, 10666));
}

TEST(HostPort, NoWarningWhenTheServerGotWhatItAskedFor)
{
	EXPECT_FALSE(PortDriftNeedsWarning(10666, 10666));
}

TEST(HostPort, NoWarningBeforeAnythingIsRunning)
{
	// There is nothing to have drifted from yet, and warning about a port you are still typing
	// would fire on every keystroke.
	EXPECT_FALSE(PortDriftNeedsWarning(0, 10666));
	EXPECT_FALSE(PortDriftNeedsWarning(-5, 10666));
}

TEST(HostPort, AnUnreadableConfiguredPortIsNotTheServersFault)
{
	// Callers clamp the field to a default, so this is belt and braces; accusing the server of
	// moving because the form was empty would be a warning nobody can act on.
	EXPECT_FALSE(PortDriftNeedsWarning(10670, 0));
	EXPECT_FALSE(PortDriftNeedsWarning(10670, -1));
}

// ---------------------------------------------------------------- the regression this exists for

namespace
{

ProbeCacheKey KeyFor(int port)
{
	ProbeCacheKey key;
	key.publicIp = "203.0.113.9";
	key.localSubnet = "192.168.1";
	key.port = port;
	return key;
}

} // namespace

TEST(HostPort, AVerdictForTheConfiguredPortIsNeverShownForAServerRunningElsewhere)
{
	// THE BUG, pinned end to end across the two units that have to agree.
	//
	// You forward 10666, something else is holding it, the server takes 10670. The check had already
	// recorded "reachable" about 10666 -- which was true -- and the panel went green while nobody
	// outside could reach the server at all. It told the player their hosting was fine. That is the
	// worst way to be wrong, because a confident answer stops you looking.
	//
	// Two things have to hold together for that to be impossible, and neither is sufficient alone:
	// the port asked about must be the one the server holds, and the cache must refuse to answer for
	// a port it never tested. This asserts both in the order they actually run.
	const int configured = 10666;
	const int running = 10670;

	const int asked = PortToCheck(running, configured);
	EXPECT_EQ(running, asked) << "the check must follow the server, not the form";

	EXPECT_FALSE(ProbeCacheKeyMatches(KeyFor(configured), KeyFor(asked)))
		<< "a verdict about 10666 must not be reused for 10670";
}

TEST(HostPort, TheCachedVerdictIsStillUsedWhenTheServerGotThePortItAskedFor)
{
	// The other half: this must not become so strict that an ordinary host throws away a perfectly
	// good answer and shows unknown forever.
	const int configured = 10666;
	const int running = 10666;

	const int asked = PortToCheck(running, configured);
	EXPECT_EQ(configured, asked);
	EXPECT_TRUE(ProbeCacheKeyMatches(KeyFor(configured), KeyFor(asked)));
}

TEST(HostPort, BeforeHostingTheFieldsVerdictIsTheRightOneToShow)
{
	// And before anything runs, the cached answer for the typed port is exactly what should appear.
	const int asked = PortToCheck(0, 10666);
	EXPECT_TRUE(ProbeCacheKeyMatches(KeyFor(10666), KeyFor(asked)));
}
