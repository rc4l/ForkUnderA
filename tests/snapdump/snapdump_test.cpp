//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: snapdump_test.cpp
//
// Description: What the dumper does with a recording it cannot read. The
// reference test holds it to three correct recordings; these hold it to the
// refusals, which decide what "decodes byte for byte" is allowed to mean.
//
//-----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <stdio.h>
#include <string>
#include <vector>

#include "snapdump/snapdump.h"

namespace
{

// [Kaori-Chani] The committed wide reference, the one recording that ships in the repo, so these
// tests build their malformed inputs by damaging something that is known good.
const char* const REF = ZX_SNAPSHOT_TESTDATA "/ref-wide.csnap";

std::vector<unsigned char> snapdumptest_Read(const char* pszPath)
{
	std::vector<unsigned char> out;
	FILE* f = fopen(pszPath, "rb");
	if (f == NULL)
		return out;
	unsigned char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out.insert(out.end(), buf, buf + n);
	fclose(f);
	return out;
}

// [Kaori-Chani] Writes one malformed input and dumps it, returning both the result and what the
// dumper wrote, so a test can hold it to the error line and not only to the status. The result is
// also snap_dump's exit code, so these are the tool's exit codes under another name.
ESnapDumpResult snapdumptest_Dump(const std::vector<unsigned char>& in, const char* pszName,
	std::string& text)
{
	text.clear();
	const std::string inPath = std::string(ZX_SNAPSHOT_TESTOUT) + "/" + pszName;
	const std::string outPath = inPath + ".dump.txt";
	FILE* f = fopen(inPath.c_str(), "wb");
	if (f == NULL)
	{
		ADD_FAILURE() << inPath << " could not be written, so nothing was measured";
		return SNAPDUMP_UNREADABLE;
	}
	if (in.empty() == false)
		fwrite(&in[0], 1, in.size(), f);
	fclose(f);

	FILE* o = fopen(outPath.c_str(), "wb");
	if (o == NULL)
	{
		ADD_FAILURE() << outPath << " could not be written, so nothing was measured";
		return SNAPDUMP_UNREADABLE;
	}
	const ESnapDumpResult res = SnapDumpWrite(o, inPath.c_str());
	fclose(o);

	const std::vector<unsigned char> back = snapdumptest_Read(outPath.c_str());
	text.assign(back.begin(), back.end());
	return res;
}

bool snapdumptest_Has(const std::string& text, const char* pszWhat)
{
	return text.find(pszWhat) != std::string::npos;
}

} // namespace

// [Kaori-Chani] The contrast the refusals below are measured against: without it a dumper that
// only ever returned false would pass every one of them.
TEST(SnapshotDump, TheReferenceItselfIsAccepted)
{
	const std::vector<unsigned char> whole = snapdumptest_Read(REF);
	ASSERT_FALSE(whole.empty()) << REF << " could not be read";
	std::string text;
	EXPECT_EQ(SNAPDUMP_OK, snapdumptest_Dump(whole, "dump-good.csnap", text));
	EXPECT_TRUE(snapdumptest_Has(text, "csnap version=")) << "it names the container it read";
	EXPECT_TRUE(snapdumptest_Has(text, "errors=0")) << "and reports no error";
}

// [Kaori-Chani] Neither container's magic, so no decoder is chosen at all and nothing is written:
// the dumper must not guess from the file's name, which is why it reads the magic itself.
TEST(SnapshotDump, AFileThatIsNoContainerIsRefusedWithoutWritingALine)
{
	static const char szJunk[] = "not a recording at all, just some text";
	const std::vector<unsigned char> junk(szJunk, szJunk + sizeof(szJunk) - 1);
	std::string text;
	EXPECT_EQ(SNAPDUMP_BAD, snapdumptest_Dump(junk, "dump-junk.csnap", text))
		<< "read, and wrong: the bytes were there, they just name no container";
	EXPECT_EQ("", text) << "a file it cannot place leaves no partial dump behind";
}

// [Kaori-Chani] Shorter than the four magic bytes, which is the first thing read: an empty file is
// the shape a recording has when the writer was killed before its header reached the disk.
TEST(SnapshotDump, AFileShorterThanTheMagicIsRefused)
{
	std::string text;
	EXPECT_EQ(SNAPDUMP_UNREADABLE, snapdumptest_Dump(std::vector<unsigned char>(), "dump-empty.csnap", text))
		<< "no four bytes to read, which is the one failure about the file itself";
	EXPECT_EQ("", text);

	const std::vector<unsigned char> whole = snapdumptest_Read(REF);
	ASSERT_GE(whole.size(), (size_t)3);
	const std::vector<unsigned char> two(whole.begin(), whole.begin() + 2);
	EXPECT_EQ(SNAPDUMP_UNREADABLE, snapdumptest_Dump(two, "dump-two.csnap", text));
	EXPECT_EQ("", text);
}

// [Kaori-Chani] The magic reads, so a container is chosen, but what follows is cut before the
// records begin: at 64 bytes the reader refuses the cvar block and says so on stderr. The open
// fails and the dump is left empty, rather than holding a header with no records under it.
TEST(SnapshotDump, AContainerWhoseHeaderIsCutIsRefusedBeforeAnyRecord)
{
	const std::vector<unsigned char> whole = snapdumptest_Read(REF);
	ASSERT_GE(whole.size(), (size_t)64);
	const std::vector<unsigned char> head(whole.begin(), whole.begin() + 64);
	std::string text;
	EXPECT_EQ(SNAPDUMP_BAD, snapdumptest_Dump(head, "dump-shorthead.csnap", text))
		<< "the magic read, so this is the recording being wrong, not the file";
	EXPECT_EQ("", text) << "no record is written from a container that did not open";
}

// [Kaori-Chani] A recording that stops mid-record, which is what a crash during a capture leaves.
// The records before the cut still print, and the cut names itself and is counted: a dump that
// stopped silently here would read as a complete recording that happened to be short.
TEST(SnapshotDump, ARecordCutOffMidPayloadIsNamedAndCounted)
{
	const std::vector<unsigned char> whole = snapdumptest_Read(REF);
	ASSERT_GE(whole.size(), (size_t)1024);
	const std::vector<unsigned char> cut(whole.begin(), whole.begin() + 1024);
	std::string text;
	EXPECT_EQ(SNAPDUMP_BAD, snapdumptest_Dump(cut, "dump-cut.csnap", text));
	EXPECT_TRUE(snapdumptest_Has(text, "error what=truncated record=9"))
		<< "the cut names the record it fell in";
	EXPECT_TRUE(snapdumptest_Has(text, "errors=1"))
		<< "and is counted, so the totals line disagrees with a clean decode";
	EXPECT_TRUE(snapdumptest_Has(text, "end records=8"))
		<< "the records before the cut are still reported";
}

// [Kaori-Chani] A payload whose length survived but whose bytes did not: four 0xFF bytes make an
// id gap longer than the three a 16-bit gap can need, which the id reader refuses. This is the
// walk's own refusal rather than the container's, and it carries the offset inside the record.
TEST(SnapshotDump, AMalformedPayloadIsNamedAtItsOffsetInTheRecord)
{
	std::vector<unsigned char> bad = snapdumptest_Read(REF);
	ASSERT_GE(bad.size(), (size_t)166);
	for (size_t i = 162; i < 166; ++i)
		bad[i] = 0xFF;
	std::string text;
	EXPECT_EQ(SNAPDUMP_BAD, snapdumptest_Dump(bad, "dump-badpayload.csnap", text));
	EXPECT_TRUE(snapdumptest_Has(text, "error what=id offset=14"))
		<< "the walk names what it could not read and where";
	EXPECT_TRUE(snapdumptest_Has(text, "errors=1"));
}
