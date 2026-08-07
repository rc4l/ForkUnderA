// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/joinintent_compute.h"

using zx::DecideJoinIntent;
using zx::JoinIntent;

// ---------------------------------------------------------------- the reported bug

TEST(JoinIntent, JoiningTheServerYouAreHostingDoesNothingButClose)
{
	// [rc4l] The whole reason this unit exists. Hosting, connected to our own server, and JOIN
	// pressed on its row: this used to stop the server and reload the engine, so the player lost the
	// server AND their place in it, to arrive where they already were.
	EXPECT_EQ(JoinIntent::AlreadyThere, DecideJoinIntent(true, true, true));
}

TEST(JoinIntent, AlreadyThereBeatsTheHostingWarning)
{
	// Both conditions hold at once for a host on their own row. If the hosting check ran first they
	// would be asked whether to stop their server in order to go nowhere.
	const JoinIntent intent = DecideJoinIntent(true, true, true);
	EXPECT_NE(JoinIntent::ConfirmStopHosting, intent);
	EXPECT_NE(JoinIntent::Join, intent);
}

// ---------------------------------------------------------------- leaving for somewhere else

TEST(JoinIntent, LeavingYourOwnServerForAnotherAsksFirst)
{
	EXPECT_EQ(JoinIntent::ConfirmStopHosting, DecideJoinIntent(true, true, false));
}

TEST(JoinIntent, HostingWithoutBeingConnectedStillAsks)
{
	// A server that is starting, or that the player never joined, is still theirs to lose.
	EXPECT_EQ(JoinIntent::ConfirmStopHosting, DecideJoinIntent(true, false, false));
}

// ---------------------------------------------------------------- the ordinary case

TEST(JoinIntent, AnOrdinaryJoinIsNotInterferedWith)
{
	EXPECT_EQ(JoinIntent::Join, DecideJoinIntent(false, false, false));
}

TEST(JoinIntent, RejoiningAServerYouAreOnIsAlsoNothingToDo)
{
	// Not hosting, but already connected to the row that was pressed. Reloading the engine to arrive
	// where we are would cost the player their place for nothing.
	EXPECT_EQ(JoinIntent::AlreadyThere, DecideJoinIntent(false, true, true));
}

TEST(JoinIntent, ConnectedElsewhereAndNotHostingIsAPlainJoin)
{
	EXPECT_EQ(JoinIntent::Join, DecideJoinIntent(false, true, false));
}

TEST(JoinIntent, TheTargetFlagIsIgnoredWhenNotConnected)
{
	// Nothing can be "the server we are on" when we are on none, so this must not read as AlreadyThere
	// no matter what the caller passes.
	EXPECT_EQ(JoinIntent::Join, DecideJoinIntent(false, false, true));
	EXPECT_EQ(JoinIntent::ConfirmStopHosting, DecideJoinIntent(true, false, true));
}
