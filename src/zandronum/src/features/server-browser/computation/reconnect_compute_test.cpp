// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-browser/computation/reconnect_compute.h"

using namespace zx;

TEST( Reconnect, AStoredServerIsAskedAboutBeforeConnecting )
{
	// The bug this fixes: reconnect went straight to the stored address, which works only while the
	// mapping the original join opened is still alive, and it is not minutes later.
	EXPECT_EQ( ReconnectAction::AskThenConnect, DecideReconnect( true ));
}

TEST( Reconnect, NothingStoredIsRefusedRatherThanGuessedAt )
{
	EXPECT_EQ( ReconnectAction::Refuse, DecideReconnect( false ));
}

TEST( Reconnect, AskingNeverStopsTheConnection )
{
	// The property the whole design rests on: the punch runs beside the connection, never in front
	// of it, so a registry that is down or a punch that fails leaves us exactly where the old
	// behaviour left us.
	EXPECT_TRUE( ReconnectConnects( ReconnectAction::AskThenConnect ));
	EXPECT_FALSE( ReconnectConnects( ReconnectAction::Refuse ));
}

TEST( Reconnect, HavingAnAddressAlwaysMeansAttemptIt )
{
	// Whatever else changes about the decision, this must not: the only reason to refuse is having
	// nowhere to go.
	EXPECT_TRUE( ReconnectConnects( DecideReconnect( true )));
	EXPECT_FALSE( ReconnectConnects( DecideReconnect( false )));
}
