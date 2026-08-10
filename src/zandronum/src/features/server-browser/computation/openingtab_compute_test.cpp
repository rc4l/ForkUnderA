// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/openingtab_compute.h"

using zx::ComputeOpeningTab;
using zx::OpeningTab;

TEST( OpeningTab, ServersOnTheListMeanTheListIsWhereYouLand )
{
	EXPECT_EQ( OpeningTab::Browse, ComputeOpeningTab( 1, true ));
	EXPECT_EQ( OpeningTab::Browse, ComputeOpeningTab( 40, true ));
}

TEST( OpeningTab, WeLookedAndFoundNothingSoHostingIsTheOnlyThingLeft )
{
	// The whole reason the exception exists: an empty list is a dead end, and the tab beside it is
	// the only thing on the screen the player can still act on.
	EXPECT_EQ( OpeningTab::Host, ComputeOpeningTab( 0, true ));
}

TEST( OpeningTab, AColdSessionOpensOnTheListAndStaysThere )
{
	// Nothing listed yet because nothing has come back yet. "Empty" and "not looked" are different
	// answers, and only the first sends the player to the other tab. Sending them on the second
	// would open the browser on HOST every single cold start.
	EXPECT_EQ( OpeningTab::Browse, ComputeOpeningTab( 0, false ));
}

TEST( OpeningTab, ServersInHandOutrankAnUnfinishedRefresh )
{
	// A second visit while a refresh is still running: the rows from last time are on screen and
	// are what the player came back for.
	EXPECT_EQ( OpeningTab::Browse, ComputeOpeningTab( 3, false ));
}

TEST( OpeningTab, ANonsenseCountIsNotServers )
{
	// The count is passed in from a size that has been clamped and re-clamped upstream. Reading a
	// negative as "some servers" would be the wrong way to be wrong.
	EXPECT_EQ( OpeningTab::Host, ComputeOpeningTab( -1, true ));
	EXPECT_EQ( OpeningTab::Browse, ComputeOpeningTab( -1, false ));
}
