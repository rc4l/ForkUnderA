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

TEST(ContinueRecord, TheStateLivesInAFolderOfItsOwn)
{
	// Named after what it is, so clearing this feature's state is one obvious action rather than
	// knowing which loose files in the config root happened to belong to it.
	EXPECT_EQ("/home/p/.config/ForkUnderA/continue", ContinueDir("/home/p/.config/ForkUnderA"));
	EXPECT_EQ("/home/p/.config/ForkUnderA/continue/session.txt",
		ContinueRecordPath("/home/p/.config/ForkUnderA"));
	EXPECT_EQ("/home/p/.config/ForkUnderA/continue/session.zds",
		ContinueSavePath("/home/p/.config/ForkUnderA"));
}

TEST(ContinueRecord, TheRecordAndItsSnapshotShareTheFolder)
{
	// They only mean anything together: a record naming a snapshot that is somewhere else is a
	// record that can be half-deleted.
	const std::string dir = ContinueDir("/a/b");
	EXPECT_EQ(dir + "/session.txt", ContinueRecordPath("/a/b"));
	EXPECT_EQ(dir + "/session.zds", ContinueSavePath("/a/b"));
}

TEST(ContinueRecord, ATrailingSeparatorIsNotDoubled)
{
	EXPECT_EQ("/a/b/continue/session.txt", ContinueRecordPath("/a/b/"));
	EXPECT_EQ("C:\\a\\continue/session.txt", ContinueRecordPath("C:\\a\\"));
}

TEST(ContinueRecord, NoRootGivesABareFolder)
{
	EXPECT_EQ("continue", ContinueDir(""));
	EXPECT_EQ("continue/session.txt", ContinueRecordPath(""));
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
