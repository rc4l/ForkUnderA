// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/joinintent_compute.h"

using zx::DecideJoinIntent;
using zx::JoinIntent;
using zx::RowIsOwnServer;

namespace
{

// The shape the bug actually had: hosting on 10666, connected to it over loopback, and the row in
// front of us wearing this machine's LAN address.
const int kHostPort = 10666;
const char *const kLocal = "192.168.1.81";
const char *const kPublic = "99.137.156.174";

JoinIntent Decide(bool holds, bool connected, bool targetIsCurrent,
	bool targetIsOwn = false, bool onOwn = false)
{
	return DecideJoinIntent(holds, connected, targetIsCurrent, targetIsOwn, onOwn);
}

} // namespace

// ---------------------------------------------------------------- the reported bug

TEST(JoinIntent, JoiningTheServerYouAreHostingDoesNothingButClose)
{
	// [rc4l] The whole reason this unit exists. Hosting, connected to our own server, and JOIN
	// pressed on its row: this used to stop the server and reload the engine, so the player lost the
	// server AND their place in it, to arrive where they already were.
	EXPECT_EQ(JoinIntent::AlreadyThere, Decide(true, true, true));
}

TEST(JoinIntent, AlreadyThereBeatsTheHostingWarning)
{
	// Both conditions hold at once for a host on their own row. If the hosting check ran first they
	// would be asked whether to stop their server in order to go nowhere.
	const JoinIntent intent = Decide(true, true, true);
	EXPECT_NE(JoinIntent::ConfirmStopHosting, intent);
	EXPECT_NE(JoinIntent::Join, intent);
}

// ---------------------------------------------------------------- the bug the first fix missed

TEST(JoinIntent, OurOwnServerUnderADifferentAddressIsStillOurOwnServer)
{
	// [rc4l] The exact reproduction. bTargetIsCurrentServer is FALSE here, because the row says
	// 192.168.1.81:10666 and we are connected on 127.0.0.1:10666, so the narrow test above misses.
	// It used to fall through to ConfirmStopHosting, which offered to stop the server we were in.
	EXPECT_EQ(JoinIntent::AlreadyThere, Decide(true, true, false, true, true));
}

TEST(JoinIntent, OurOwnServerWeAreNotOnIsConnectedToRatherThanStopped)
{
	// Holding a server we are not currently in. Joining it is exactly what the player asked for and
	// costs nobody anything, so it must not be confused with leaving for somewhere else.
	EXPECT_EQ(JoinIntent::RejoinOwnServer, Decide(true, false, false, true, false));
}

TEST(JoinIntent, ConnectedElsewhereWhileOurServerRunsStillRejoinsRatherThanStops)
{
	// Connected, but not to our own server, and the row IS our own server. Going there does not end
	// it, so there is nothing to warn about.
	EXPECT_EQ(JoinIntent::RejoinOwnServer, Decide(true, true, false, true, false));
}

TEST(JoinIntent, TheOwnServerFlagMeansNothingWithoutAServerToOwn)
{
	// A stale flag must not invent a server we are not running.
	EXPECT_EQ(JoinIntent::Join, Decide(false, false, false, true, false));
	EXPECT_EQ(JoinIntent::Join, Decide(false, true, false, true, true));
}

// ---------------------------------------------------------------- leaving for somewhere else

TEST(JoinIntent, LeavingYourOwnServerForAnotherAsksFirst)
{
	EXPECT_EQ(JoinIntent::ConfirmStopHosting, Decide(true, true, false));
}

TEST(JoinIntent, HostingWithoutBeingConnectedStillAsks)
{
	// A server that is starting, or that the player never joined, is still theirs to lose.
	EXPECT_EQ(JoinIntent::ConfirmStopHosting, Decide(true, false, false));
}

// ---------------------------------------------------------------- the ordinary case

TEST(JoinIntent, AnOrdinaryJoinIsNotInterferedWith)
{
	EXPECT_EQ(JoinIntent::Join, Decide(false, false, false));
}

TEST(JoinIntent, RejoiningAServerYouAreOnIsAlsoNothingToDo)
{
	// Not hosting, but already connected to the row that was pressed. Reloading the engine to arrive
	// where we are would cost the player their place for nothing.
	EXPECT_EQ(JoinIntent::AlreadyThere, Decide(false, true, true));
}

TEST(JoinIntent, ConnectedElsewhereAndNotHostingIsAPlainJoin)
{
	EXPECT_EQ(JoinIntent::Join, Decide(false, true, false));
}

TEST(JoinIntent, TheTargetFlagIsIgnoredWhenNotConnected)
{
	// Nothing can be "the server we are on" when we are on none, so this must not read as AlreadyThere
	// no matter what the caller passes.
	EXPECT_EQ(JoinIntent::Join, Decide(false, false, true));
	EXPECT_EQ(JoinIntent::ConfirmStopHosting, Decide(true, false, true));
}

// ---------------------------------------------------------------- recognising our own row

TEST(RowIsOwn, LoopbackOnOurPortIsOurs)
{
	EXPECT_TRUE(RowIsOwnServer("127.0.0.1", kHostPort, kHostPort, kLocal, kPublic));
	EXPECT_TRUE(RowIsOwnServer("localhost", kHostPort, kHostPort, kLocal, kPublic));
}

TEST(RowIsOwn, OurLanAddressIsOurs)
{
	// What LAN discovery puts in the list.
	EXPECT_TRUE(RowIsOwnServer(kLocal, kHostPort, kHostPort, kLocal, kPublic));
}

TEST(RowIsOwn, OurPublicAddressIsOurs)
{
	// What the registry puts in the list. The same server, listed a second time.
	EXPECT_TRUE(RowIsOwnServer(kPublic, kHostPort, kHostPort, kLocal, kPublic));
}

TEST(RowIsOwn, SomebodyElseIsNot)
{
	EXPECT_FALSE(RowIsOwnServer("203.0.113.9", kHostPort, kHostPort, kLocal, kPublic));
}

TEST(RowIsOwn, TheSameMachineOnAnotherPortIsAnotherServer)
{
	// Two servers on one machine is ordinary. Matching on address alone would make the second one
	// unjoinable while the first was running.
	EXPECT_FALSE(RowIsOwnServer(kLocal, 10667, kHostPort, kLocal, kPublic));
	EXPECT_FALSE(RowIsOwnServer("127.0.0.1", 10667, kHostPort, kLocal, kPublic));
}

TEST(RowIsOwn, AnUnknownAddressMatchesNothingRatherThanEverything)
{
	// [rc4l] Empty means "we were never told", which must not behave as a wildcard: the public IP is
	// unset until the reachability probe has answered, and a wildcard there would make every row on
	// our port read as ours during exactly that window.
	EXPECT_FALSE(RowIsOwnServer("203.0.113.9", kHostPort, kHostPort, "", ""));
	EXPECT_FALSE(RowIsOwnServer("", kHostPort, kHostPort, "", ""));

	// Loopback still resolves without either of them, because it needs no lookup to be certain.
	EXPECT_TRUE(RowIsOwnServer("127.0.0.1", kHostPort, kHostPort, "", ""));
}

TEST(RowIsOwn, NoPortMeansNoAnswer)
{
	// A row whose port never arrived, and a host port of zero because nothing is running.
	EXPECT_FALSE(RowIsOwnServer(kLocal, 0, kHostPort, kLocal, kPublic));
	EXPECT_FALSE(RowIsOwnServer(kLocal, kHostPort, 0, kLocal, kPublic));
}
