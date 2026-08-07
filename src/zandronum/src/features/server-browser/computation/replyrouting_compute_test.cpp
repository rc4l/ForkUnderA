// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "replyrouting_compute.h"

using namespace zx;

namespace
{

// The real values from networkshared.h. Hard-coded here on purpose: if either constant ever moves,
// this test is where that shows up rather than in a player's empty server list.
const LauncherCommands kCommands = { 5660023, 5660032 };

const long kWhole = 5660023;
const long kSegmented = 5660032;

} // namespace

// ---------------------------------------------------------------- routing a reply

TEST( ReplyRouting, ALauncherReplyWeAskedForGoesToTheBrowser )
{
	EXPECT_TRUE( ShouldRouteToBrowser( true, kWhole, kCommands ));
	EXPECT_TRUE( ShouldRouteToBrowser( true, kSegmented, kCommands ));
}

TEST( ReplyRouting, GameTrafficIsLeftAlone )
{
	// The ordinary case, and the one that must never break: we are playing, we asked this server
	// nothing, and every packet it sends belongs to the game.
	EXPECT_FALSE( ShouldRouteToBrowser( false, kWhole, kCommands ));
	EXPECT_FALSE( ShouldRouteToBrowser( false, kSegmented, kCommands ));
}

TEST( ReplyRouting, AnUnaskedPacketIsNeverStolenFromTheGame )
{
	// [rc4l] The whole reason the gate exists. A game packet is free to begin with any four bytes,
	// including these -- so without an outstanding question, matching the command means nothing and
	// acting on it would take a real game packet away from the parser that needed it.
	EXPECT_FALSE( ShouldRouteToBrowser( false, kWhole, kCommands ));
}

TEST( ReplyRouting, SomethingElseFromAServerWeAskedStillGoesToTheGame )
{
	// We are waiting on this address, but this particular packet is not the answer. Game traffic
	// keeps flowing while a query is outstanding, so this is the common case, not an edge one.
	EXPECT_FALSE( ShouldRouteToBrowser( true, 1, kCommands ));
	EXPECT_FALSE( ShouldRouteToBrowser( true, 0, kCommands ));
	EXPECT_FALSE( ShouldRouteToBrowser( true, -1, kCommands ));
	EXPECT_FALSE( ShouldRouteToBrowser( true, kWhole - 1, kCommands ));
	EXPECT_FALSE( ShouldRouteToBrowser( true, kSegmented + 1, kCommands ));
}

// ---------------------------------------------------------------- explaining an empty list

TEST( ReplyRouting, NobodyHostingIsTheAnswerWhenNothingWentWrong )
{
	EXPECT_EQ( EmptyReason::NothingHosted, ExplainEmptyList( false, 0, 0, 0 ));
}

TEST( ReplyRouting, TheSearchIsBlamedBeforeAnythingElse )
{
	// Servers are there and answering; the player's own filter is what emptied the screen.
	EXPECT_EQ( EmptyReason::HiddenBySearch, ExplainEmptyList( true, 6, 0, 0 ));

	// And it wins even when other things also went wrong, because it is the one the player undoes
	// by pressing backspace.
	EXPECT_EQ( EmptyReason::HiddenBySearch, ExplainEmptyList( true, 6, 3, 2 ));
}

TEST( ReplyRouting, ASearchOverNoServersIsNotTheSearchsFault )
{
	// Nothing survived because nothing arrived. Saying "hidden by the search" here would send the
	// player to clear a box that was never the problem.
	EXPECT_EQ( EmptyReason::NothingHosted, ExplainEmptyList( true, 0, 0, 0 ));
	EXPECT_EQ( EmptyReason::NoResponse, ExplainEmptyList( true, 0, 0, 4 ));
}

TEST( ReplyRouting, AWrongBuildBeatsATimeout )
{
	// [rc4l] Deliberate ordering. A mismatch is a definite answer from a server that replied; a
	// timeout is the absence of one. The first tells a player to update, the second sends them to
	// their router -- so when both are true, the actionable one is what they are told.
	EXPECT_EQ( EmptyReason::WrongVersion, ExplainEmptyList( false, 0, 1, 5 ));
}

TEST( ReplyRouting, TimeoutsAreReportedWhenTheyAreAllThereIs )
{
	EXPECT_EQ( EmptyReason::NoResponse, ExplainEmptyList( false, 0, 0, 2 ));
}

TEST( ReplyRouting, TheHostedServerCaseFromTheBugReport )
{
	// Two servers listed, both silent, one of them the server the player was standing on. Before the
	// routing fix this was the whole screen: "no servers found", "2 did not respond".
	EXPECT_EQ( EmptyReason::NoResponse, ExplainEmptyList( false, 0, 0, 2 ));

	// And afterwards the one we are connected to answers, so it is no longer part of the count.
	EXPECT_EQ( EmptyReason::NoResponse, ExplainEmptyList( false, 1, 0, 1 ));
}
