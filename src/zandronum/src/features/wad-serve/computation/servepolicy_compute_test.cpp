// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/wad-serve/computation/servepolicy_compute.h"

using zx::ClassifyServeRequest;
using zx::ComputeAdmitTransfer;
using zx::ComputeRetryAfterSeconds;
using zx::FindServableFile;
using zx::ServableFile;
using zx::ServeVerdict;
using zx::ServeVerdictReason;
using zx::ServeVerdictStatus;
using std::string;
using std::vector;

namespace
{
vector<ServableFile> Loaded()
{
	vector<ServableFile> files;
	files.push_back(ServableFile("freedoom2.wad", true, 20 * 1024 * 1024));
	files.push_back(ServableFile("dwango5.wad", false, 2109396));
	files.push_back(ServableFile("Brutal.wad", false, 40 * 1024 * 1024));
	return files;
}

ServeVerdict Classify(const string &requested, long long maxBytes = 0)
{
	int index = 0;
	return ClassifyServeRequest(Loaded(), requested, true, maxBytes, index);
}
} // namespace

// ---------------------------------------------------------------- lookup

TEST(FindServableFile, MatchesRegardlessOfCase)
{
	// The request came off a network, and the same WAD is filed under three spellings across the
	// ecosystem -- the operator did not get to choose which one a client would ask with.
	EXPECT_EQ(1, FindServableFile(Loaded(), "dwango5.wad"));
	EXPECT_EQ(1, FindServableFile(Loaded(), "DWANGO5.WAD"));
	EXPECT_EQ(2, FindServableFile(Loaded(), "brutal.wad"));
}

TEST(FindServableFile, ReportsAMissAsMinusOne)
{
	EXPECT_EQ(-1, FindServableFile(Loaded(), "nothere.wad"));
	EXPECT_EQ(-1, FindServableFile(Loaded(), "dwango5.wa")) << "a prefix is not a match";
	EXPECT_EQ(-1, FindServableFile(vector<ServableFile>(), "dwango5.wad"));
}

// ---------------------------------------------------------------- the serving decision

TEST(ClassifyServeRequest, ServesAPwadTheServerHasOpen)
{
	int index = -1;
	EXPECT_EQ(ServeVerdict::Allowed,
		ClassifyServeRequest(Loaded(), "dwango5.wad", true, 0, index));
	EXPECT_EQ(1, index);
}

TEST(ClassifyServeRequest, ServesAnIwadOnTheFreeList)
{
	// Freedoom is on config/iwadallowlist.txt, so a LAN party with no internet can get it from the
	// server it is joining.
	int index = -1;
	EXPECT_EQ(ServeVerdict::Allowed,
		ClassifyServeRequest(Loaded(), "freedoom2.wad", true, 0, index));
	EXPECT_EQ(0, index);
}

TEST(ClassifyServeRequest, RefusesAnIwadNotOnTheFreeList)
{
	// The guard rail on a carelessly configured operator: a ZandroX server must not become the thing
	// that distributes doom2.wad.
	vector<ServableFile> files;
	files.push_back(ServableFile("doom2.wad", true, 14604584));

	int index = -1;
	EXPECT_EQ(ServeVerdict::ProtectedIwad,
		ClassifyServeRequest(files, "doom2.wad", true, 0, index));
	EXPECT_EQ(-1, index);
}

TEST(ClassifyServeRequest, JudgesTheIwadFlagRatherThanTheName)
{
	// A PWAD called doom2.wad is a mod with an unfortunate filename, not the game. The IWAD slot is
	// what the engine actually loaded it into, and that is what the rule keys off.
	vector<ServableFile> files;
	files.push_back(ServableFile("doom2.wad", false, 1024));

	int index = -1;
	EXPECT_EQ(ServeVerdict::Allowed, ClassifyServeRequest(files, "doom2.wad", true, 0, index));
}

TEST(ClassifyServeRequest, RefusesAFileTheServerDoesNotHave)
{
	int index = 7;
	EXPECT_EQ(ServeVerdict::NotLoaded,
		ClassifyServeRequest(Loaded(), "secrets.cfg", true, 0, index));
	EXPECT_EQ(-1, index) << "the index must not survive a refusal";
}

TEST(ClassifyServeRequest, RefusesEverythingWhenSwitchedOff)
{
	int index = 7;
	EXPECT_EQ(ServeVerdict::Disabled,
		ClassifyServeRequest(Loaded(), "dwango5.wad", false, 0, index));
	EXPECT_EQ(-1, index);
}

TEST(ClassifyServeRequest, RefusesAFilePastTheCeiling)
{
	EXPECT_EQ(ServeVerdict::TooLarge, Classify("Brutal.wad", 1024 * 1024));
	EXPECT_EQ(ServeVerdict::Allowed, Classify("Brutal.wad", 100 * 1024 * 1024));
	EXPECT_EQ(ServeVerdict::Allowed, Classify("Brutal.wad", 0)) << "0 means no ceiling";
}

// ---------------------------------------------------------------- reporting

TEST(ServeVerdict, EveryVerdictHasAReasonAndAStatus)
{
	const ServeVerdict all[] = {
		ServeVerdict::Allowed, ServeVerdict::Disabled, ServeVerdict::NotLoaded,
		ServeVerdict::ProtectedIwad, ServeVerdict::TooLarge,
	};

	for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
	{
		const char *reason = ServeVerdictReason(all[i]);
		ASSERT_NE(nullptr, reason);
		EXPECT_NE('\0', reason[0]) << "verdict " << i << " has an empty reason";
		EXPECT_GE(ServeVerdictStatus(all[i]), 200);
	}
}

TEST(ServeVerdict, DisabledIsIndistinguishableFromMissing)
{
	// Deliberate: a server that has serving off should not advertise a feature it declines to give.
	EXPECT_EQ(404, ServeVerdictStatus(ServeVerdict::Disabled));
	EXPECT_EQ(404, ServeVerdictStatus(ServeVerdict::NotLoaded));
	EXPECT_EQ(200, ServeVerdictStatus(ServeVerdict::Allowed));
	EXPECT_EQ(403, ServeVerdictStatus(ServeVerdict::ProtectedIwad));
	EXPECT_EQ(403, ServeVerdictStatus(ServeVerdict::TooLarge));
}

TEST(ServeVerdict, AnUnknownVerdictStillRefusesSafely)
{
	// If someone adds an enumerator and forgets these switches, the fallback must deny rather than
	// serve. Reaching it needs a cast -- that is the point.
	const ServeVerdict bogus = static_cast<ServeVerdict>(9999);
	EXPECT_STREQ("refused", ServeVerdictReason(bogus));
	EXPECT_EQ(403, ServeVerdictStatus(bogus));
}

// ---------------------------------------------------------------- admission

TEST(ComputeAdmitTransfer, AdmitsWhileSlotsRemain)
{
	EXPECT_TRUE(ComputeAdmitTransfer(0, 4, 0, 2));
	EXPECT_TRUE(ComputeAdmitTransfer(3, 4, 1, 2));
}

TEST(ComputeAdmitTransfer, RefusesWhenAllSlotsAreBusy)
{
	EXPECT_FALSE(ComputeAdmitTransfer(4, 4, 0, 2));
	EXPECT_FALSE(ComputeAdmitTransfer(5, 4, 0, 2));
}

TEST(ComputeAdmitTransfer, RefusesOnePeerTakingEverySlot)
{
	// Twenty connections from one address would otherwise hold the whole server: free for the
	// attacker, expensive for everyone else.
	EXPECT_FALSE(ComputeAdmitTransfer(2, 8, 2, 2));
	EXPECT_TRUE(ComputeAdmitTransfer(2, 8, 2, 0)) << "0 means no per-address cap";
}

TEST(ComputeAdmitTransfer, RefusesEverythingWithNoSlotsConfigured)
{
	EXPECT_FALSE(ComputeAdmitTransfer(0, 0, 0, 2));
	EXPECT_FALSE(ComputeAdmitTransfer(0, -1, 0, 2));
}

TEST(ComputeRetryAfter, GrowsInWavesOfTheSlotCount)
{
	// Four slots means the first four clients wait one transfer, the next four wait two.
	EXPECT_EQ(60, ComputeRetryAfterSeconds(0, 4, 60));
	EXPECT_EQ(60, ComputeRetryAfterSeconds(3, 4, 60));
	EXPECT_EQ(120, ComputeRetryAfterSeconds(4, 4, 60));
	EXPECT_EQ(180, ComputeRetryAfterSeconds(8, 4, 60));
}

TEST(ComputeRetryAfter, IsNeverInstantAndNeverAbsurd)
{
	// Below a second a client hammers us; past a few minutes it gives up and calls it a failure.
	EXPECT_EQ(1, ComputeRetryAfterSeconds(0, 4, 0));
	EXPECT_EQ(300, ComputeRetryAfterSeconds(400, 4, 60));
}

TEST(ComputeRetryAfter, SurvivesNonsensicalInputs)
{
	EXPECT_EQ(1, ComputeRetryAfterSeconds(-5, 0, -1));
	EXPECT_GE(ComputeRetryAfterSeconds(-5, 4, 30), 1);
}

TEST( ServableFile, DefaultsToSomethingThatCannotBeServedByAccident )
{
	// The table is filled in by the engine as it walks its loaded WADs, so entries exist before they
	// are described. An entry that defaulted to "IWAD" would put the allowlist gate in front of a
	// nameless file; one that defaulted to a non-zero size would be a ceiling check against a number
	// nobody supplied.
	const ServableFile fresh;

	EXPECT_TRUE( fresh.name.empty( ));
	EXPECT_FALSE( fresh.isIwad );
	EXPECT_EQ( 0, fresh.size );
}
