// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/ownjoin_compute.h"

using zx::DecideOwnJoin;
using zx::OwnJoinAction;
using zx::OwnJoinIn;
using zx::OwnJoinOut;

namespace
{

OwnJoinIn Hosting(bool remembered, bool rebuildable)
{
	OwnJoinIn in;
	in.hostingCatalogueEntry = true;
	in.haveRememberedFiles = remembered;
	in.canRebuildFiles = rebuildable;
	return in;
}

} // namespace

// ------------------------------------------------------------ the case that was already right

TEST(OwnJoin, ACustomSetupIsAlreadyRunningWhatTheServerIs)
{
	// The server's command line came from ours, so there is nothing to reload onto. Reloading here
	// would tear the game down to arrive back where it started.
	OwnJoinIn in;
	in.hostingCatalogueEntry = false;

	EXPECT_EQ(OwnJoinAction::ConnectDirectly, DecideOwnJoin(in).action);
}

TEST(OwnJoin, ACustomSetupDoesNotCareWhetherAnythingWasRemembered)
{
	// Swept, because the remembered list belongs to catalogue hosting and must not start deciding
	// anything for the case that has no entry at all.
	for (int remembered = 0; remembered < 2; ++remembered)
	{
		for (int rebuildable = 0; rebuildable < 2; ++rebuildable)
		{
			OwnJoinIn in;
			in.hostingCatalogueEntry = false;
			in.haveRememberedFiles = ( remembered != 0 );
			in.canRebuildFiles = ( rebuildable != 0 );

			EXPECT_EQ(OwnJoinAction::ConnectDirectly, DecideOwnJoin(in).action)
				<< "remembered=" << remembered << " rebuildable=" << rebuildable;
		}
	}
}

// ------------------------------------------------------------ reloading

TEST(OwnJoin, AnEntryWithItsListStillInHandReloadsOntoIt)
{
	const OwnJoinOut out = DecideOwnJoin(Hosting(true, false));

	EXPECT_EQ(OwnJoinAction::ReloadThenConnect, out.action);
	EXPECT_FALSE(out.useRebuilt);
}

TEST(OwnJoin, TheRememberedListWinsOverARebuild)
{
	// [rc4l] They should agree and do not have to. A file replaced on disk between starting the
	// server and joining it makes the rebuild right about the disk and wrong about the server, and
	// the server is the thing we have to match.
	const OwnJoinOut out = DecideOwnJoin(Hosting(true, true));

	EXPECT_EQ(OwnJoinAction::ReloadThenConnect, out.action);
	EXPECT_FALSE(out.useRebuilt) << "the list the server was handed is the one to reload onto";
}

TEST(OwnJoin, AForgottenListIsRebuiltRatherThanGivenUpOn)
{
	// THE regression. The list is filled in when the server starts and cleared the first time it is
	// used, so any second pass through here arrives with nothing -- and used to connect anyway.
	const OwnJoinOut out = DecideOwnJoin(Hosting(false, true));

	EXPECT_EQ(OwnJoinAction::ReloadThenConnect, out.action);
	EXPECT_TRUE(out.useRebuilt);
}

// ------------------------------------------------------------ refusing

TEST(OwnJoin, WithNothingToReloadOntoItRefusesRatherThanConnecting)
{
	// Connecting is not a gamble that might come off: the server has the entry's files, this client
	// does not, and authentication compares them.
	const OwnJoinOut out = DecideOwnJoin(Hosting(false, false));

	EXPECT_EQ(OwnJoinAction::Refuse, out.action);
	EXPECT_FALSE(out.refusal.empty()) << "a refusal that says nothing is the bug in another form";
}

TEST(OwnJoin, TheRefusalSaysTheServerIsStillUp)
{
	// It is: only the join failed. Without that the player stops a server they could still play on
	// by pressing the button again after fixing the file.
	const OwnJoinOut out = DecideOwnJoin(Hosting(false, false));

	EXPECT_NE(std::string::npos, out.refusal.find("still running"));
}

TEST(OwnJoin, NothingElseEverRefuses)
{
	// A refusal stops the player getting into a server that is up, so it has to be reachable from
	// exactly one combination and no other.
	for (int entry = 0; entry < 2; ++entry)
	{
		for (int remembered = 0; remembered < 2; ++remembered)
		{
			for (int rebuildable = 0; rebuildable < 2; ++rebuildable)
			{
				OwnJoinIn in;
				in.hostingCatalogueEntry = ( entry != 0 );
				in.haveRememberedFiles = ( remembered != 0 );
				in.canRebuildFiles = ( rebuildable != 0 );

				const bool bShouldRefuse = ( entry != 0 ) && ( remembered == 0 ) && ( rebuildable == 0 );
				const OwnJoinOut out = DecideOwnJoin(in);

				EXPECT_EQ(bShouldRefuse, out.action == OwnJoinAction::Refuse)
					<< "entry=" << entry << " remembered=" << remembered
					<< " rebuildable=" << rebuildable;

				EXPECT_EQ(bShouldRefuse, !out.refusal.empty()) << "a reason is given exactly when refusing";
			}
		}
	}
}

TEST(OwnJoin, HostingAnEntryNeverConnectsWithoutReloading)
{
	// The whole point, stated as its own sweep: whatever else happens, an entry's server is never
	// joined on whatever files the client happened to be carrying.
	for (int remembered = 0; remembered < 2; ++remembered)
	{
		for (int rebuildable = 0; rebuildable < 2; ++rebuildable)
		{
			const OwnJoinOut out = DecideOwnJoin(Hosting(remembered != 0, rebuildable != 0));

			EXPECT_NE(OwnJoinAction::ConnectDirectly, out.action)
				<< "remembered=" << remembered << " rebuildable=" << rebuildable;
		}
	}
}
