// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/continue/computation/continuewrite_compute.h"

using namespace zx;

namespace
{

ContinueWriteInputs InAMap()
{
	ContinueWriteInputs in;
	in.inMap = true;
	return in;
}

} // namespace

TEST(ContinueWrite, QuittingFromInsideAMapIsRecorded)
{
	EXPECT_EQ(ContinueWriteVerdict::Write, DecideContinueWrite(InAMap()));
}

TEST(ContinueWrite, ACrashIsNeverRecorded)
{
	// The reason this is not a shutdown hook at all: atexit(call_terms) means the atterm chain runs
	// on I_FatalError's exit() exactly as it does on a clean quit, so a record written from there
	// would faithfully save the crash.
	ContinueWriteInputs in = InAMap();
	in.crashing = true;
	EXPECT_EQ(ContinueWriteVerdict::Skip, DecideContinueWrite(in));
}

TEST(ContinueWrite, QuittingMidConnectIsNotRecorded)
{
	// The old session is gone and the new one never arrived, so recording either would be a lie.
	ContinueWriteInputs in = InAMap();
	in.connecting = true;
	EXPECT_EQ(ContinueWriteVerdict::Skip, DecideContinueWrite(in));
}

TEST(ContinueWrite, QuittingFromAMenuIsNotRecorded)
{
	ContinueWriteInputs in;
	in.inMap = false;
	EXPECT_EQ(ContinueWriteVerdict::Skip, DecideContinueWrite(in));
}

TEST(ContinueWrite, ACrashOutranksEverythingElse)
{
	// Checked first because it is the one that can be true alongside anything, and the one whose
	// cost is highest: it destroys a good record AND offers the player a session they fled.
	ContinueWriteInputs in;
	in.inMap = true;
	in.connecting = false;
	in.crashing = true;
	EXPECT_EQ(ContinueWriteVerdict::Skip, DecideContinueWrite(in));
}

TEST(ContinueWrite, OnlyOneCombinationEverWrites)
{
	// Every doubtful case skips, because overwriting a good session with a bad one is the failure
	// that matters.
	for (int map = 0; map <= 1; ++map)
	{
		for (int conn = 0; conn <= 1; ++conn)
		{
			for (int crash = 0; crash <= 1; ++crash)
			{
				ContinueWriteInputs in;
				in.inMap = (map == 1);
				in.connecting = (conn == 1);
				in.crashing = (crash == 1);

				const bool expected = (map == 1) && (conn == 0) && (crash == 0);
				EXPECT_EQ(expected, DecideContinueWrite(in) == ContinueWriteVerdict::Write)
					<< "map=" << map << " conn=" << conn << " crash=" << crash;
			}
		}
	}
}

TEST(ContinueWrite, ASuccessfulJoinIsRecorded)
{
	// Nothing to doubt here: the player is demonstrably in.
	EXPECT_EQ(ContinueWriteVerdict::Write, DecideContinueWriteOnJoin(false));
}

TEST(ContinueWrite, AJoinThatLandsAsWeAreDyingIsNot)
{
	EXPECT_EQ(ContinueWriteVerdict::Skip, DecideContinueWriteOnJoin(true));
}

TEST( ContinueWrite, WhileHostingTheLocalMapIsNotSnapshotted )
{
	// Joining our own server comes straight back through here. Snapshotting then replaced the record
	// of the game we are HOSTING with whatever map we happened to be standing in when we started it,
	// so leaving the hosted game later took the player to that map instead of starting it again.
	ContinueWriteInputs in = InAMap();
	in.hosting = true;

	EXPECT_EQ( ContinueWriteVerdict::Skip, DecideContinueWrite( in ));
}

TEST( ContinueWrite, HostingOutranksBeingInAPerfectlyGoodMap )
{
	// The point is that everything else about the moment looks writable: in a level, not connecting,
	// not crashing. Only the fact that our own server is up says otherwise.
	for ( int host = 0; host <= 1; ++host )
	{
		ContinueWriteInputs in = InAMap();
		in.hosting = ( host == 1 );

		const bool expected = ( host == 0 );
		EXPECT_EQ( expected, DecideContinueWrite( in ) == ContinueWriteVerdict::Write );
	}
}
