// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/continue/computation/continuebutton_compute.h"

using namespace zx;

namespace
{

ContinueButtonInputs AtMenu()
{
	return ContinueButtonInputs();
}

ContinueButtonInputs InSession()
{
	ContinueButtonInputs in;
	in.inSession = true;
	return in;
}

} // namespace

TEST( ContinueButton, NothingToOfferHidesIt )
{
	EXPECT_EQ( ContinueMode::Hidden, DecideContinueButton( AtMenu() ).mode );
}

TEST( ContinueButton, OutOfASessionItOffersTheOfflineGame )
{
	ContinueButtonInputs in = AtMenu();
	in.offlineUsable = true;

	const ContinueButtonVerdict v = DecideContinueButton( in );
	EXPECT_EQ( ContinueMode::Continue, v.mode );
	EXPECT_EQ( ContinueTarget::Offline, v.target );
}

TEST( ContinueButton, OutOfASessionItOffersTheServerWhenThatIsAllThereIs )
{
	ContinueButtonInputs in = AtMenu();
	in.serverUsable = true;

	const ContinueButtonVerdict v = DecideContinueButton( in );
	EXPECT_EQ( ContinueMode::Continue, v.mode );
	EXPECT_EQ( ContinueTarget::Server, v.target );
}

TEST( ContinueButton, WithBothTheMoreRecentlyLeftWins )
{
	// The whole point of decoupling them: joining a server no longer forgets the campaign, and
	// finishing with the server does not forget the server either.
	ContinueButtonInputs in = AtMenu();
	in.offlineUsable = true;
	in.serverUsable = true;

	in.offlineStamp = 7;
	in.serverStamp = 9;
	EXPECT_EQ( ContinueTarget::Server, DecideContinueButton( in ).target );

	in.offlineStamp = 11;
	EXPECT_EQ( ContinueTarget::Offline, DecideContinueButton( in ).target );
}

TEST( ContinueButton, ATieGoesToTheOfflineGame )
{
	// A tie is what leaving an offline game FOR a server looks like, both written in one breath. Of
	// that pair the server is where the player already is; the offline game is the thing they left.
	ContinueButtonInputs in = AtMenu();
	in.offlineUsable = true;
	in.serverUsable = true;
	in.offlineStamp = 5;
	in.serverStamp = 5;

	EXPECT_EQ( ContinueTarget::Offline, DecideContinueButton( in ).target );
}

// ---------------------------------------------------------------- in a session

TEST( ContinueButton, InASessionItBecomesDisconnect )
{
	EXPECT_EQ( ContinueMode::Disconnect, DecideContinueButton( InSession() ).mode );
}

TEST( ContinueButton, DisconnectLandsInTheOfflineGameWhenThereIsOne )
{
	ContinueButtonInputs in = InSession();
	in.offlineUsable = true;

	EXPECT_EQ( ContinueTarget::Offline, DecideContinueButton( in ).target );
}

TEST( ContinueButton, DisconnectFallsBackToTheMainMenu )
{
	// Joined straight from the browser with nothing behind it, which is most people most of the
	// time. It must still land somewhere deliberate rather than a bare console.
	EXPECT_EQ( ContinueTarget::MainMenu, DecideContinueButton( InSession() ).target );
}

TEST( ContinueButton, ARememberedServerNeverAffectsWhereDisconnectLands )
{
	// We are IN a server; the remembered one is beside the point, even a newer one.
	ContinueButtonInputs in = InSession();
	in.serverUsable = true;
	in.serverStamp = 99;

	EXPECT_EQ( ContinueTarget::MainMenu, DecideContinueButton( in ).target );

	in.offlineUsable = true;
	in.offlineStamp = 1;
	EXPECT_EQ( ContinueTarget::Offline, DecideContinueButton( in ).target );
}

TEST( ContinueButton, TheButtonIsNeverHiddenWhileInASession )
{
	// Leaving is always possible, so the way out is always there.
	for ( int off = 0; off <= 1; ++off )
	{
		for ( int srv = 0; srv <= 1; ++srv )
		{
			ContinueButtonInputs in = InSession();
			in.offlineUsable = ( off == 1 );
			in.serverUsable = ( srv == 1 );

			EXPECT_EQ( ContinueMode::Disconnect, DecideContinueButton( in ).mode );
			EXPECT_NE( ContinueTarget::None, DecideContinueButton( in ).target );
		}
	}
}
