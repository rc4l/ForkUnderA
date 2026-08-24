// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/continue/computation/continuerecord_compute.h"

using namespace zx;

namespace
{

ContinueRecord ServerRecord()
{
	ContinueRecord r;
	r.kind = ContinueKind::Server;
	r.address = "1.2.3.4:10666";
	r.password = "hunter2";
	r.iwad = "doom2.wad";
	r.iwadHash = "25e1459ca71d7f2eafd11e6d0e9ff54b";
	r.wads.push_back(std::make_pair(std::string("dadm.pk3"), std::string("aaaa")));
	r.wads.push_back(std::make_pair(std::string("sunset.wad"), std::string("bbbb")));
	return r;
}

ContinueRecord SingleRecord()
{
	ContinueRecord r;
	r.kind = ContinueKind::Single;
	r.savePath = "/home/p/continue.zds";
	r.saveVersion = 4512;
	r.mapName = "MAP07";
	r.iwad = "doom2.wad";
	return r;
}

ContinueRecord RoundTrip(const ContinueRecord &in)
{
	ContinueRecord out;
	EXPECT_TRUE(ParseContinue(SerialiseContinue(in), out));
	return out;
}

} // namespace

TEST(ContinueRecord, AServerSessionSurvivesTheRoundTrip)
{
	const ContinueRecord in = ServerRecord();
	const ContinueRecord out = RoundTrip(in);

	EXPECT_EQ(ContinueKind::Server, out.kind);
	EXPECT_EQ(in.address, out.address);
	EXPECT_EQ(in.password, out.password);
	EXPECT_EQ(in.iwad, out.iwad);
	EXPECT_EQ(in.iwadHash, out.iwadHash);
	ASSERT_EQ(2u, out.wads.size());
	EXPECT_EQ("dadm.pk3", out.wads[0].first);
	EXPECT_EQ("aaaa", out.wads[0].second);
	EXPECT_EQ("sunset.wad", out.wads[1].first);
}

TEST(ContinueRecord, AnOfflineSessionSurvivesTheRoundTrip)
{
	const ContinueRecord out = RoundTrip(SingleRecord());

	EXPECT_EQ(ContinueKind::Single, out.kind);
	EXPECT_EQ("/home/p/continue.zds", out.savePath);
	EXPECT_EQ(4512, out.saveVersion);
	EXPECT_EQ("MAP07", out.mapName);
}

TEST(ContinueRecord, NothingToContinueRendersNothing)
{
	// So a caller that writes the result unconditionally still cannot leave half a record behind.
	ContinueRecord empty;
	EXPECT_EQ("", SerialiseContinue(empty));
}

TEST(ContinueRecord, APasswordWithSpacesSurvivesVerbatim)
{
	// The whole rest of the line is the value; splitting on every space would corrupt exactly the
	// fields that have to come back byte for byte.
	ContinueRecord in = ServerRecord();
	in.password = "correct horse battery staple";

	EXPECT_EQ(in.password, RoundTrip(in).password);
}

TEST(ContinueRecord, AWadNameWithSpacesSurvivesVerbatim)
{
	ContinueRecord in = ServerRecord();
	in.wads.clear();
	in.wads.push_back(std::make_pair(std::string("my great mod.pk3"), std::string("cccc")));

	const ContinueRecord out = RoundTrip(in);
	ASSERT_EQ(1u, out.wads.size());
	EXPECT_EQ("my great mod.pk3", out.wads[0].first);
	EXPECT_EQ("cccc", out.wads[0].second);
}

TEST(ContinueRecord, AWadWithNoHashIsKept)
{
	// What a server that sent no hashes gives us. The name alone is still worth having.
	ContinueRecord in = ServerRecord();
	in.wads.clear();
	in.wads.push_back(std::make_pair(std::string("plain.wad"), std::string()));

	const ContinueRecord out = RoundTrip(in);
	ASSERT_EQ(1u, out.wads.size());
	EXPECT_EQ("plain.wad", out.wads[0].first);
	EXPECT_EQ("", out.wads[0].second);
}

TEST(ContinueRecord, AnEmptyPasswordIsNotWrittenAndReadsBackEmpty)
{
	ContinueRecord in = ServerRecord();
	in.password.clear();

	const std::string text = SerialiseContinue(in);
	EXPECT_EQ(std::string::npos, text.find("password"));
	EXPECT_EQ("", RoundTrip(in).password);
}

// ---------------------------------------------------------------- refusing

TEST(ContinueRecord, SomethingThatIsNotOurFileIsRefused)
{
	ContinueRecord out;
	EXPECT_FALSE(ParseContinue("", out));
	EXPECT_FALSE(ParseContinue("hello\nkind server\naddress 1.2.3.4\n", out));
	EXPECT_FALSE(ParseContinue("fua-continueX 1\nkind server\naddress 1.2.3.4\n", out));
}

TEST(ContinueRecord, ARecordFromANewerEngineIsRefused)
{
	// Read hopefully, a changed field would look absent, and Continue would land somewhere
	// plausible and wrong.
	ContinueRecord out;
	EXPECT_FALSE(ParseContinue("fua-continue 2\nkind server\naddress 1.2.3.4:10666\n", out));
	EXPECT_FALSE(ParseContinue("fua-continue 99\nkind single\nsave /x\n", out));
}

TEST(ContinueRecord, AMissingOrNonsenseFormatIsRefused)
{
	ContinueRecord out;
	EXPECT_FALSE(ParseContinue("fua-continue\nkind single\nsave /x\n", out));
	EXPECT_FALSE(ParseContinue("fua-continue nope\nkind single\nsave /x\n", out));
	EXPECT_FALSE(ParseContinue("fua-continue 0\nkind single\nsave /x\n", out));
	EXPECT_FALSE(ParseContinue("fua-continue -1\nkind single\nsave /x\n", out));
}

TEST(ContinueRecord, AKindWeDoNotKnowIsRefused)
{
	ContinueRecord out;
	EXPECT_FALSE(ParseContinue("fua-continue 1\nkind demo\nsave /x\n", out));
}

TEST(ContinueRecord, ARecordWithNoKindIsRefused)
{
	ContinueRecord out;
	EXPECT_FALSE(ParseContinue("fua-continue 1\naddress 1.2.3.4:10666\n", out));
}

TEST(ContinueRecord, AKindMissingWhatItNeedsIsRefused)
{
	// Half a record is worse than none: it would offer to take the player somewhere that never was.
	ContinueRecord out;
	EXPECT_FALSE(ParseContinue("fua-continue 1\nkind single\nmap MAP07\n", out));
	EXPECT_FALSE(ParseContinue("fua-continue 1\nkind server\npassword x\n", out));
}

TEST(ContinueRecord, UnknownKeysAndBlankLinesAreIgnored)
{
	// So a field added by a later build of the SAME format costs an older reader nothing.
	ContinueRecord out;
	ASSERT_TRUE(ParseContinue(
		"fua-continue 1\n\nkind server\naddress 1.2.3.4:10666\nfuture something\n\n", out));
	EXPECT_EQ("1.2.3.4:10666", out.address);
}

TEST(ContinueRecord, ParsingClearsWhateverTheCallerHadBefore)
{
	// Otherwise a refused parse leaves the previous session's fields sitting in the output.
	ContinueRecord out = ServerRecord();
	EXPECT_FALSE(ParseContinue("rubbish", out));
	EXPECT_EQ(ContinueKind::None, out.kind);
	EXPECT_EQ("", out.address);
	EXPECT_TRUE(out.wads.empty());
}

// ---------------------------------------------------------------- path

TEST(ContinueRecord, EachInstanceGetsItsOwnFolder)
{
	// A second copy of the engine is a second player, exactly as it is for the account keys, and two
	// of them sharing one record would have each overwriting the other's session.
	EXPECT_EQ("/cfg/continue", ContinueDir("/cfg", 0));
	EXPECT_EQ("/cfg/continue.2", ContinueDir("/cfg", 1));
	EXPECT_EQ("/cfg/continue.3", ContinueDir("/cfg", 2));
}

TEST(ContinueRecord, TheTwoRecordsAreSeparateFiles)
{
	// Decoupled on purpose: joining a server must not forget the campaign, and an unreadable one
	// must not take the other down with it.
	EXPECT_EQ("/cfg/continue/offline.txt", ContinueOfflinePath("/cfg", 0));
	EXPECT_EQ("/cfg/continue/server.txt", ContinueServerPath("/cfg", 0));
	EXPECT_EQ("/cfg/continue/offline.zds", ContinueSavePath("/cfg", 0));

	EXPECT_EQ("/cfg/continue.2/offline.txt", ContinueOfflinePath("/cfg", 1));
	EXPECT_EQ("/cfg/continue.2/server.txt", ContinueServerPath("/cfg", 1));
}

TEST(ContinueRecord, ATrailingSeparatorIsNotDoubled)
{
	EXPECT_EQ("/a/b/continue/offline.txt", ContinueOfflinePath("/a/b/", 0));
	EXPECT_EQ("C:\\a\\continue/server.txt", ContinueServerPath("C:\\a\\", 0));
}

TEST(ContinueRecord, NoRootGivesABareFolder)
{
	EXPECT_EQ("continue", ContinueDir("", 0));
	EXPECT_EQ("continue.2/offline.txt", ContinueOfflinePath("", 1));
}

TEST(ContinueRecord, TheStampSurvivesTheRoundTrip)
{
	// It is how "most recently left" survives a restart without a clock.
	ContinueRecord in = SingleRecord();
	in.stamp = 42;
	EXPECT_EQ(42, RoundTrip(in).stamp);
}

TEST(ContinueRecord, TheMapsOwnWadSurvivesTheRoundTrip)
{
	ContinueRecord in = SingleRecord();
	in.mapWad = "eviternity.wad";

	EXPECT_EQ("eviternity.wad", RoundTrip(in).mapWad);
}

TEST(ContinueRecord, TheServersNameSurvivesTheRoundTrip)
{
	// Spaces and punctuation and all: a hostname is whatever the operator typed.
	ContinueRecord in = ServerRecord();
	in.serverName = "Kappa's Duel Server  [EU]";

	EXPECT_EQ("Kappa's Duel Server  [EU]", RoundTrip(in).serverName);
}

TEST(ContinueRecord, TheNewFieldsAreOptional)
{
	// They were added without bumping the format, so a record written before they existed still
	// parses and simply describes itself slightly less well.
	ContinueRecord out;
	ASSERT_TRUE(ParseContinue("fua-continue 1\nkind single\nsave /x\nmap MAP11\n", out));
	EXPECT_EQ("", out.mapWad);

	ASSERT_TRUE(ParseContinue("fua-continue 1\nkind server\naddress 1.2.3.4:10666\n", out));
	EXPECT_EQ("", out.serverName);
}

TEST(ContinueRecord, AWadLineWithNoTabIsANameOnItsOwn)
{
	// Our own writer always emits the separator, so this shape only arrives from a record written by
	// hand or by something older. It is still a usable name.
	ContinueRecord out;
	ASSERT_TRUE(ParseContinue(
		"fua-continue 1\nkind server\naddress 1.2.3.4:10666\nwad plain.wad\n", out));

	ASSERT_EQ(1u, out.wads.size());
	EXPECT_EQ("plain.wad", out.wads[0].first);
	EXPECT_EQ("", out.wads[0].second);
}

TEST(ContinueRecord, ALineWithNoKeyIsSkippedRatherThanRead)
{
	// A line that begins with a space has no key. Reading it as one would invent a field named "".
	ContinueRecord out;
	ASSERT_TRUE(ParseContinue(
		"fua-continue 1\nkind server\n   leading space\naddress 1.2.3.4:10666\n", out));

	EXPECT_EQ("1.2.3.4:10666", out.address);
}

TEST(ContinueRecord, AWadLineWithNoNameIsDropped)
{
	ContinueRecord out;
	ASSERT_TRUE(ParseContinue(
		"fua-continue 1\nkind server\naddress 1.2.3.4:10666\nwad \t\n", out));

	EXPECT_TRUE(out.wads.empty());
}

TEST(ContinueRecord, AHostedGameSurvivesTheRoundTripWholeSale)
{
	// The whole config, because a rehost has to start the same server -- not something resembling it.
	ContinueRecord in;
	in.kind = ContinueKind::Hosted;
	in.host.hostName = "Bob's DM  [EU]";
	in.host.iwad = "doom2.wad";
	in.host.pwads.push_back("skulltag_actors.pk3");
	in.host.pwads.push_back("av.wad");
	in.host.map = "D2DM01";
	in.host.execCfg = "/entries/skulltag/server.cfg";
	in.host.execRemixCfgs.push_back("/entries/skulltag/fast.cfg");
	in.host.extraCvars.push_back(std::make_pair(std::string("sv_maxlives"), std::string("3")));
	in.host.extraCvars.push_back(std::make_pair(std::string("sv_motd"), std::string("hello there friend")));
	in.host.mapRotation.push_back("D2DM01");
	in.host.mapRotation.push_back("D2DM02");
	in.host.password = "secret pass";
	in.host.joinPassword = "join me";
	in.host.gameMode = 4;
	in.host.maxPlayers = 16;
	in.host.port = 10777;
	in.host.advertise = true;
	in.host.serveWads = false;
	in.host.hideWindow = true;

	const ContinueRecord out = RoundTrip(in);
	EXPECT_EQ(ContinueKind::Hosted, out.kind);
	EXPECT_EQ("Bob's DM  [EU]", out.host.hostName);
	EXPECT_EQ("doom2.wad", out.host.iwad);
	ASSERT_EQ(2u, out.host.pwads.size());
	EXPECT_EQ("av.wad", out.host.pwads[1]);
	EXPECT_EQ("D2DM01", out.host.map);
	EXPECT_EQ("/entries/skulltag/server.cfg", out.host.execCfg);
	ASSERT_EQ(1u, out.host.execRemixCfgs.size());
	ASSERT_EQ(2u, out.host.extraCvars.size());
	EXPECT_EQ("sv_maxlives", out.host.extraCvars[0].first);
	EXPECT_EQ("3", out.host.extraCvars[0].second);
	EXPECT_EQ("hello there friend", out.host.extraCvars[1].second) << "a value with spaces must survive";
	ASSERT_EQ(2u, out.host.mapRotation.size());
	EXPECT_EQ("secret pass", out.host.password);
	EXPECT_EQ("join me", out.host.joinPassword);
	EXPECT_EQ(4, out.host.gameMode);
	EXPECT_EQ(16, out.host.maxPlayers);
	EXPECT_EQ(10777, out.host.port);
	EXPECT_TRUE(out.host.advertise);
	EXPECT_FALSE(out.host.serveWads);
	EXPECT_TRUE(out.host.hideWindow);
}

TEST(ContinueRecord, TheRconSecretIsNeverWrittenDown)
{
	// It is worth nothing after the process it was made for, so a rehost must mint a new one. Storing
	// it would be storing a dead credential and inviting somebody to replay it.
	ContinueRecord in;
	in.kind = ContinueKind::Hosted;
	in.host.map = "MAP01";
	in.host.rconSecret = "hunter2-do-not-store-me";

	const std::string text = SerialiseContinue(in);
	EXPECT_EQ(std::string::npos, text.find("hunter2-do-not-store-me"));
	EXPECT_EQ("", RoundTrip(in).host.rconSecret);
}

TEST(ContinueRecord, AHostedRecordWithNoMapIsRefused)
{
	// It would start a server on whatever the WADs default to, which is not the game we left.
	ContinueRecord out;
	EXPECT_FALSE(ParseContinue("fua-continue 1\nkind hosted\nhost_iwad doom2.wad\n", out));
}
