// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/bot-save/computation/botsave_compute.h"

#include <string>

using namespace zx;

namespace
{

BotSnapshot Bot(int slot, const char *name)
{
	BotSnapshot b;
	b.slot = slot;
	b.name = name;
	b.team = "red";
	b.forwardMove = 12800;
	b.sideMove = -6400;
	b.forwardMovePersist = true;
	b.sideMovePersist = false;
	b.buttons = 5;
	b.aimAtEnemy = true;
	b.aimAtEnemyDelay = 7;
	b.angleDelta = 0x20000000u;
	b.angleOffBy = 0x80000000u;		// past INT_MAX: an angle_t is unsigned and must survive as one
	b.angleDesired = 0xFFFFFFFFu;
	b.turnLeft = true;
	b.pathType = 2;
	b.skillIncrease = true;
	b.skillDecrease = false;
	b.lastMedalReceived = -1;
	return b;
}

std::vector<BotSnapshot> RoundTrip(const std::vector<BotSnapshot> &in)
{
	const std::vector<unsigned char> bytes = SerialiseBots(in);
	std::vector<BotSnapshot> out;
	EXPECT_TRUE(ParseBots(bytes.empty() ? 0 : &bytes[0], bytes.size(), out));
	return out;
}

} // namespace

TEST(BotSave, ABotSurvivesTheRoundTrip)
{
	std::vector<BotSnapshot> in;
	in.push_back(Bot(3, "Rambo"));

	const std::vector<BotSnapshot> out = RoundTrip(in);
	ASSERT_EQ(1u, out.size());

	EXPECT_EQ(3, out[0].slot);
	EXPECT_EQ("Rambo", out[0].name);
	EXPECT_EQ("red", out[0].team);
	EXPECT_EQ(12800, out[0].forwardMove);
	EXPECT_EQ(-6400, out[0].sideMove) << "a negative move must not come back positive";
	EXPECT_TRUE(out[0].forwardMovePersist);
	EXPECT_FALSE(out[0].sideMovePersist);
	EXPECT_EQ(5, out[0].buttons);
	EXPECT_TRUE(out[0].aimAtEnemy);
	EXPECT_EQ(7u, out[0].aimAtEnemyDelay);
	EXPECT_TRUE(out[0].turnLeft);
	EXPECT_EQ(2u, out[0].pathType);
	EXPECT_TRUE(out[0].skillIncrease);
	EXPECT_FALSE(out[0].skillDecrease);
	EXPECT_EQ(-1, out[0].lastMedalReceived);
}

TEST(BotSave, AnglesSurviveTheWholeUnsignedRange)
{
	// angle_t is unsigned and uses its top bit. Round-tripping through a signed int is how a bot
	// comes back facing the opposite way.
	std::vector<BotSnapshot> in;
	in.push_back(Bot(1, "Angle"));

	const std::vector<BotSnapshot> out = RoundTrip(in);
	EXPECT_EQ(0x20000000u, out[0].angleDelta);
	EXPECT_EQ(0x80000000u, out[0].angleOffBy);
	EXPECT_EQ(0xFFFFFFFFu, out[0].angleDesired);
}

TEST(BotSave, SeveralBotsKeepTheirOrderAndTheirSlots)
{
	std::vector<BotSnapshot> in;
	in.push_back(Bot(1, "One"));
	in.push_back(Bot(5, "Two"));
	in.push_back(Bot(7, "Three"));

	const std::vector<BotSnapshot> out = RoundTrip(in);
	ASSERT_EQ(3u, out.size());
	EXPECT_EQ(1, out[0].slot);
	EXPECT_EQ("Two", out[1].name);
	EXPECT_EQ(7, out[2].slot);
}

TEST(BotSave, ANameWithSpacesAndPunctuationSurvivesVerbatim)
{
	// Whatever the player typed. Length-prefixed rather than terminated, so this cannot be cut short.
	std::vector<BotSnapshot> in;
	in.push_back(Bot(2, "Bob \"The Nailgun\" O'Hara"));

	EXPECT_EQ("Bob \"The Nailgun\" O'Hara", RoundTrip(in)[0].name);
}

TEST(BotSave, AnEmptyNameOrTeamIsFine)
{
	std::vector<BotSnapshot> in;
	BotSnapshot b = Bot(0, "");
	b.team = "";
	in.push_back(b);

	const std::vector<BotSnapshot> out = RoundTrip(in);
	ASSERT_EQ(1u, out.size());
	EXPECT_EQ("", out[0].name);
	EXPECT_EQ("", out[0].team);
}

TEST(BotSave, NoBotsMeansNoChunkAtAll)
{
	// An absent chunk is exactly what every save written before this existed looks like, so it must
	// be the same thing as "there were no bots".
	EXPECT_TRUE(SerialiseBots(std::vector<BotSnapshot>()).empty());
}

// ---------------------------------------------------------------- refusing

TEST(BotSave, NothingAtAllIsRefused)
{
	std::vector<BotSnapshot> out;
	EXPECT_FALSE(ParseBots(0, 0, out));

	const unsigned char byte = 0;
	EXPECT_FALSE(ParseBots(&byte, 1, out));
}

TEST(BotSave, SomethingThatIsNotOurChunkIsRefused)
{
	std::vector<BotSnapshot> out;
	const char junk[] = "PNGx and then some";
	EXPECT_FALSE(ParseBots(reinterpret_cast<const unsigned char *>(junk), sizeof junk, out));
}

TEST(BotSave, AChunkFromANewerEngineIsRefused)
{
	// Half-understood bot state is worse than no bots, because no bots is a state the loader already
	// handles correctly.
	std::vector<BotSnapshot> in;
	in.push_back(Bot(1, "Rambo"));
	std::vector<unsigned char> bytes = SerialiseBots(in);
	bytes[4] = 99;

	std::vector<BotSnapshot> out;
	EXPECT_FALSE(ParseBots(&bytes[0], bytes.size(), out));
}

TEST(BotSave, ACountBeyondThePlayerSlotsIsRefused)
{
	// Bots live in player slots, so there can never be more of them than there are slots. Checked
	// before anything is allocated.
	std::vector<unsigned char> bytes;
	const char magic[4] = { 'B', 'O', 'T', 'S' };
	bytes.insert(bytes.end(), magic, magic + 4);
	for (int i = 0; i < 4; ++i) bytes.push_back(i == 0 ? 1 : 0);			// version 1
	for (int i = 0; i < 4; ++i) bytes.push_back(0xFF);					// four billion bots

	std::vector<BotSnapshot> out;
	EXPECT_FALSE(ParseBots(&bytes[0], bytes.size(), out));
}

TEST(BotSave, EveryTruncationIsRefusedRatherThanRead)
{
	// The whole point of the bounds checks: no prefix of a real chunk may parse, and none may read
	// past the end getting there.
	std::vector<BotSnapshot> in;
	in.push_back(Bot(1, "One"));
	in.push_back(Bot(2, "Two"));
	const std::vector<unsigned char> full = SerialiseBots(in);

	for (size_t n = 1; n < full.size(); ++n)
	{
		std::vector<BotSnapshot> out;
		EXPECT_FALSE(ParseBots(&full[0], n, out)) << "a chunk cut to " << n << " bytes parsed";
	}
}

TEST(BotSave, AStringClaimingMoreBytesThanRemainIsRefused)
{
	std::vector<BotSnapshot> in;
	in.push_back(Bot(1, "One"));
	std::vector<unsigned char> bytes = SerialiseBots(in);

	// The first string length sits after magic, version, count and slot.
	const size_t at = 4 + 4 + 4 + 4;
	for (int i = 0; i < 4; ++i)
		bytes[at + i] = 0xFF;

	std::vector<BotSnapshot> out;
	EXPECT_FALSE(ParseBots(&bytes[0], bytes.size(), out));
}

TEST(BotSave, ARefusedParseLeavesNothingBehind)
{
	// Otherwise a caller that ignores the return value carries on with half a roster.
	std::vector<BotSnapshot> out;
	out.push_back(Bot(9, "Stale"));

	EXPECT_FALSE(ParseBots(0, 0, out));
	EXPECT_TRUE(out.empty());
}

TEST(BotSave, TheLargestAllowedRosterStillParses)
{
	std::vector<BotSnapshot> in;
	for (int i = 0; i < kBotSaveMaxBots; ++i)
		in.push_back(Bot(i, "Bot"));

	EXPECT_EQ(static_cast<size_t>(kBotSaveMaxBots), RoundTrip(in).size());
}
