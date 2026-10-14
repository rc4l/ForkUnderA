// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/continue/computation/continueshow_compute.h"

using namespace zx;

namespace
{

ContinueShowInputs Single()
{
	ContinueShowInputs in;
	in.recordParsed = true;
	in.kind = ContinueKind::Single;
	in.saveFileExists = true;
	in.saveVersion = 4512;
	in.minSaveVersion = 4507;
	return in;
}

ContinueShowInputs Server(ServerProbe probe)
{
	ContinueShowInputs in;
	in.recordParsed = true;
	in.kind = ContinueKind::Server;
	in.probe = probe;
	return in;
}

} // namespace

TEST(ContinueShow, NothingToContinueMeansNoButton)
{
	ContinueShowInputs in;
	EXPECT_FALSE(ContinueIsShown(in));
}

TEST(ContinueShow, ARecordThatDidNotParseMeansNoButton)
{
	ContinueShowInputs in = Single();
	in.recordParsed = false;
	EXPECT_FALSE(ContinueIsShown(in));
}

TEST(ContinueShow, AnOfflineSessionShows)
{
	EXPECT_EQ(ContinueVisibility::Shown, DecideContinueVisibility(Single()));
}

TEST(ContinueShow, AMissingSnapshotHidesIt)
{
	// The file the record points at was deleted, moved, or never written.
	ContinueShowInputs in = Single();
	in.saveFileExists = false;
	EXPECT_FALSE(ContinueIsShown(in));
}

TEST(ContinueShow, ASnapshotThisBuildCannotReadHidesIt)
{
	// Asked here rather than at load time, because by load time the WAD set has already been swapped
	// and the player is looking at a torn-down menu.
	ContinueShowInputs in = Single();
	in.saveVersion = 4506;
	EXPECT_FALSE(ContinueIsShown(in));
}

TEST(ContinueShow, ASnapshotExactlyAtTheMinimumStillShows)
{
	// The boundary is inclusive, matching G_LoadGame's own test.
	ContinueShowInputs in = Single();
	in.saveVersion = in.minSaveVersion;
	EXPECT_TRUE(ContinueIsShown(in));
}

TEST(ContinueShow, ALiveServerShows)
{
	EXPECT_TRUE(ContinueIsShown(Server(ServerProbe::Alive)));
}

TEST(ContinueShow, AServerThatIsGoneHidesIt)
{
	EXPECT_FALSE(ContinueIsShown(Server(ServerProbe::Gone)));
}

TEST(ContinueShow, AServerRunningSomethingElseHidesIt)
{
	// Same address, different game. Rejoining would download a stranger's WAD set.
	EXPECT_FALSE(ContinueIsShown(Server(ServerProbe::WadsDiffer)));
}

TEST(ContinueShow, WhileTheProbeIsPendingItStillShows)
{
	// The asymmetry is the point, and it is the same one headerreach_compute settles: hiding until
	// an answer arrives makes the button appear a second late, under the cursor, which turns a
	// misclick into a reconnect. Showing costs at worst one press that lands back in the browser.
	EXPECT_TRUE(ContinueIsShown(Server(ServerProbe::Unknown)));
}

TEST(ContinueShow, TheSaveVersionIsIrrelevantToAServerRecord)
{
	// There is no snapshot involved in rejoining, so a stale version field must not hide it.
	ContinueShowInputs in = Server(ServerProbe::Alive);
	in.saveVersion = 1;
	in.minSaveVersion = 9999;
	EXPECT_TRUE(ContinueIsShown(in));
}

TEST(ContinueShow, AProbeVerdictIsIrrelevantToAnOfflineRecord)
{
	ContinueShowInputs in = Single();
	in.probe = ServerProbe::Gone;
	EXPECT_TRUE(ContinueIsShown(in));
}

TEST(ContinueShow, EveryHidingReasonHidesOnItsOwn)
{
	// The property that matters more than any single case: no combination of facts can talk the
	// button back into existence once one disqualifying reason holds.
	for (int missing = 0; missing <= 1; ++missing)
	{
		for (int old = 0; old <= 1; ++old)
		{
			ContinueShowInputs in = Single();
			in.saveFileExists = (missing == 0);
			in.saveVersion = (old == 1) ? 1 : 4512;

			const bool expected = (missing == 0) && (old == 0);
			EXPECT_EQ(expected, ContinueIsShown(in)) << "missing=" << missing << " old=" << old;
		}
	}
}

TEST(ContinueShow, ARecordThatParsedButNamesNoKindShowsNothing)
{
	// Reachable in its own right: the file was readable and the caller said so, but there is no
	// session in it. Belt and braces against a kind added later that nothing here handles yet.
	ContinueShowInputs in;
	in.recordParsed = true;
	in.kind = ContinueKind::None;
	in.saveFileExists = true;
	in.saveVersion = 9999;

	EXPECT_EQ(ContinueVisibility::Hidden, DecideContinueVisibility(in));
}

TEST( ContinueShow, AHostedGameIsAlwaysOfferable )
{
	// It needs no save and answers no probe -- starting it again is entirely within our gift.
	ContinueShowInputs in;
	in.recordParsed = true;
	in.kind = ContinueKind::Hosted;
	in.minSaveVersion = 4507;

	EXPECT_TRUE( ContinueIsShown( in ));

	// And a probe verdict about somebody else's server has no bearing on ours.
	in.probe = ServerProbe::Gone;
	EXPECT_TRUE( ContinueIsShown( in ));
}
