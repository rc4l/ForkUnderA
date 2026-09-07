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

TEST( ContinueButton, OneEntryIsOfferedWithoutAList )
{
	// The feature started as one press and one press is still right when there is nothing to choose
	// between: a menu of a single row is a click that asks a question with one answer.
	ContinueButtonInputs in = AtMenu();
	in.offerableCount = 1;
	in.listCount = 1;
	in.newestTarget = ContinueTarget::Offline;

	const ContinueButtonVerdict v = DecideContinueButton( in );
	EXPECT_EQ( ContinueMode::Continue, v.mode );
	EXPECT_EQ( ContinueTarget::Offline, v.target );
	EXPECT_FALSE( v.opensList );
}

TEST( ContinueButton, TwoOrMoreEntriesOpenTheList )
{
	ContinueButtonInputs in = AtMenu();
	in.newestTarget = ContinueTarget::Server;
	in.offerableCount = 1;			// however few are pressable, the rows are what is being chosen from

	for ( int count = 2; count <= 50; ++count )
	{
		in.listCount = count;

		const ContinueButtonVerdict v = DecideContinueButton( in );
		EXPECT_EQ( ContinueMode::Continue, v.mode ) << "count " << count;
		EXPECT_TRUE( v.opensList ) << "count " << count;
	}
}

TEST( ContinueButton, ThePillNamesTheNewestEntry )
{
	// Whatever the list holds, the label and tooltip describe what ONE press would do, which is the
	// most recent thing. A pill that named the second row would be describing a menu, not an action.
	const ContinueTarget kinds[] =
		{ ContinueTarget::Offline, ContinueTarget::Hosted, ContinueTarget::Server };

	for ( int i = 0; i < 3; ++i )
	{
		ContinueButtonInputs in = AtMenu();
		in.offerableCount = 4;
		in.listCount = 4;
		in.newestTarget = kinds[i];

		EXPECT_EQ( kinds[i], DecideContinueButton( in ).target );
	}
}

TEST( ContinueButton, AnEmptyHistoryHidesItHoweverItIsDescribed )
{
	// A newest target with no entries behind it is a contradiction, and the count is the one that
	// decides: offering a row that is not there is the failure the whole unit exists to avoid.
	ContinueButtonInputs in = AtMenu();
	in.newestTarget = ContinueTarget::Server;
	in.offerableCount = 0;
	in.listCount = 3;				// rows we could show, none of which a press could act on

	EXPECT_EQ( ContinueMode::Hidden, DecideContinueButton( in ).mode );

	in.offerableCount = -1;			// a count that has gone wrong is still not something to offer
	EXPECT_EQ( ContinueMode::Hidden, DecideContinueButton( in ).mode );
}

// ---------------------------------------------------------------- in a session

TEST( ContinueButton, InASessionItBecomesDisconnect )
{
	EXPECT_EQ( ContinueMode::Disconnect, DecideContinueButton( InSession() ).mode );
}

TEST( ContinueButton, DisconnectLandsInTheOfflineGameWhenThereIsOne )
{
	ContinueButtonInputs in = InSession();
	in.localUsable = true;

	EXPECT_EQ( ContinueTarget::Offline, DecideContinueButton( in ).target );
}

TEST( ContinueButton, DisconnectFallsBackToTheMainMenu )
{
	// Joined straight from the browser with nothing behind it, which is most people most of the
	// time. It must still land somewhere deliberate rather than a bare console.
	EXPECT_EQ( ContinueTarget::MainMenu, DecideContinueButton( InSession() ).target );
}

TEST( ContinueButton, LeavingNeverOpensTheList )
{
	// Disconnect is one act with one destination. A player who wants somewhere else can open the
	// list once they are out; asking them WHERE while they are still connected turns leaving into a
	// two-step decision they did not ask to make.
	ContinueButtonInputs in = InSession();
	in.offerableCount = 30;
	in.listCount = 30;
	in.localUsable = true;

	EXPECT_FALSE( DecideContinueButton( in ).opensList );
}

TEST( ContinueButton, TheButtonIsNeverHiddenWhileInASession )
{
	// Leaving is always possible, so the way out is always there.
	for ( int local = 0; local <= 1; ++local )
	{
		for ( int count = 0; count <= 3; ++count )
		{
			ContinueButtonInputs in = InSession();
			in.localUsable = ( local == 1 );
			in.offerableCount = count;
			in.listCount = count;

			EXPECT_EQ( ContinueMode::Disconnect, DecideContinueButton( in ).mode );
			EXPECT_NE( ContinueTarget::None, DecideContinueButton( in ).target );
		}
	}
}

TEST( ContinueButton, AHostedGameIsStartedAgainRatherThanLoaded )
{
	// The world lived in a child process and went with it, so there is nothing to restore but the
	// settings that made it.
	ContinueButtonInputs in = AtMenu();
	in.offerableCount = 1;
	in.listCount = 1;
	in.newestTarget = ContinueTarget::Hosted;

	EXPECT_EQ( ContinueTarget::Hosted, DecideContinueButton( in ).target );
}

TEST( ContinueButton, DisconnectGoesBackToTheGameWeWereHosting )
{
	// Nested: hosting, then out to a real server, then out again.
	ContinueButtonInputs in = InSession();
	in.localUsable = true;
	in.localIsHosted = true;

	const ContinueButtonVerdict v = DecideContinueButton( in );
	EXPECT_EQ( ContinueMode::Disconnect, v.mode );
	EXPECT_EQ( ContinueTarget::Hosted, v.target );
}

TEST( ContinueButton, LeavingNeverSendsYouBackToTheServerYouLeft )
{
	// The regression this encodes: the destination used to be worked out AFTER the teardown, when
	// the pill answers the out-of-session question and offers the most recently left thing -- which
	// right after a kick is the server that just threw you out. In a session the answer comes from
	// the local entry alone, so however full the history is of servers it cannot be one of them.
	for ( int count = 1; count <= 50; count += 7 )
	{
		ContinueButtonInputs in = InSession();
		in.offerableCount = count;
		in.listCount = count;
		in.newestTarget = ContinueTarget::Server;
		in.localUsable = true;

		EXPECT_EQ( ContinueTarget::Offline, DecideContinueButton( in ).target )
			<< "a history of " << count << " changed where leaving lands";
	}
}

TEST( ContinueButton, ARowThatCannotBePressedStillCountsAsSomethingToChooseFrom )
{
	// The bug this encodes, found by pressing it: a history of a dead server and a game to rehost
	// showed TWO rows, only one of which was pressable, so the pill decided there was nothing to ask
	// about and threw the player straight into a rehost they never chose. What is in front of them is
	// what makes it a question.
	ContinueButtonInputs in = AtMenu();
	in.listCount = 2;
	in.offerableCount = 1;
	in.newestTarget = ContinueTarget::Hosted;

	const ContinueButtonVerdict v = DecideContinueButton( in );
	EXPECT_EQ( ContinueMode::Continue, v.mode );
	EXPECT_TRUE( v.opensList );
}

TEST( ContinueButton, ASingleRowIsStillOnePress )
{
	// And the other side of it: one row is one row however it is counted, and must not gain a menu.
	ContinueButtonInputs in = AtMenu();
	in.listCount = 1;
	in.offerableCount = 1;
	in.newestTarget = ContinueTarget::Server;

	EXPECT_FALSE( DecideContinueButton( in ).opensList );
}
