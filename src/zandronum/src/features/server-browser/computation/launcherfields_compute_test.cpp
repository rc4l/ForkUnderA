// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/launcherfields_compute.h"

using zx::ExtendedParse;
using zx::KnownExtendedFields;
using zx::LauncherExtendedInfo;
using zx::ParseExtendedInfo;
using std::string;
using std::vector;

namespace
{

const unsigned kAllBits =
	zx::kSqf2PwadHashes | zx::kSqf2Country | zx::kSqf2GameModeName | zx::kSqf2GameModeShortName |
	zx::kSqf2VoiceChat | zx::kSqf2DirectDownload | zx::kSqf2IwadHash | zx::kSqf2WadSizes;

const int kPort = 10777;					// 0x2A19 -- the value from the real bug

// Emits the wire format the SERVER produces, so a test can assert the parser consumes exactly what a
// writer wrote. Deliberately independent of the parser: if both had the same bug they would agree.
struct Writer
{
	vector<unsigned char> bytes;

	void Byte(int v) { bytes.push_back(static_cast<unsigned char>(v & 0xFF)); }
	void Short(int v) { Byte(v & 0xFF); Byte((v >> 8) & 0xFF); }
	void Long(unsigned long v)
	{
		for (int shift = 0; shift < 32; shift += 8)
			Byte(static_cast<int>((v >> shift) & 0xFF));
	}
	void Raw(const string &s) { for (size_t i = 0; i < s.size(); ++i) Byte(s[i]); }
	void Str(const string &s) { Raw(s); Byte(0); }
};

// The fields in ASCENDING BIT ORDER, which is what the server emits -- its dispatch table is a map
// keyed by bit value, so bit 0 goes first and bit 6 last.
vector<unsigned char> BuildBlock(unsigned flags2)
{
	Writer w;

	if (flags2 & zx::kSqf2PwadHashes)
	{
		w.Byte(2);
		w.Str("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
		w.Str("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
	}
	if (flags2 & zx::kSqf2Country)
		w.Raw("XIP");						// three bytes, NO terminator
	if (flags2 & zx::kSqf2GameModeName)
		w.Str("Cooperative");
	if (flags2 & zx::kSqf2GameModeShortName)
		w.Str("COOP");
	if (flags2 & zx::kSqf2VoiceChat)
		w.Byte(1);							// sv_allowvoicechat -- the byte that caused the bug
	if (flags2 & zx::kSqf2DirectDownload)
	{
		w.Byte(0);							// flags: prefers-mirrors clear
		w.Short(kPort);
	}
	if (flags2 & zx::kSqf2IwadHash)
		w.Str("cd666466759b5e5f63af93c5f0ffd0a1");
	if (flags2 & zx::kSqf2WadSizes)
	{
		w.Byte(2);
		w.Long(14263296UL);					// doom2.wad, comfortably inside a signed int
		w.Long(3221225472UL);				// 3 GB -- has its top bit set, so a signed read goes negative
	}

	return w.bytes;
}

ExtendedParse Parse(const vector<unsigned char> &bytes, unsigned flags2, LauncherExtendedInfo &out,
	size_t &consumed)
{
	return ParseExtendedInfo(bytes.empty() ? NULL : &bytes[0], bytes.size(), flags2, out, consumed);
}

} // namespace

// ---------------------------------------------------------------- the regression

TEST(LauncherFields, TheVoiceChatByteDoesNotShiftTheDownloadPort)
{
	// THE BUG. SQF2_VOICECHAT is bit 4, immediately before the download field at bit 5, and writes
	// one byte the browser used to skip. Everything after it then read one byte early: 10777 is
	// 0x2A19, so the wire holds [flags 0x00][0x19][0x2A], and reading one early takes voicechat's 1
	// as the flags byte and [0x00][0x19] as the port -- 0x1900 = 6400. A plausible number rather
	// than obvious garbage, which is why it survived so long.
	const unsigned flags = zx::kSqf2VoiceChat | zx::kSqf2DirectDownload;

	LauncherExtendedInfo info;
	size_t consumed = 0;
	ASSERT_EQ(ExtendedParse::Ok, Parse(BuildBlock(flags), flags, info, consumed));

	EXPECT_EQ(kPort, info.directDownloadPort) << "6400 here means the voicechat byte was skipped";
	EXPECT_FALSE(info.prefersMirrors) << "a set flag here means voicechat's 1 was read as our flags";
}

TEST(LauncherFields, TheSameFieldsParseWithAndWithoutVoiceChat)
{
	LauncherExtendedInfo withOut, withIn;
	size_t a = 0, b = 0;

	const unsigned withoutVoice = zx::kSqf2DirectDownload | zx::kSqf2IwadHash;
	const unsigned withVoice = withoutVoice | zx::kSqf2VoiceChat;

	ASSERT_EQ(ExtendedParse::Ok, Parse(BuildBlock(withoutVoice), withoutVoice, withOut, a));
	ASSERT_EQ(ExtendedParse::Ok, Parse(BuildBlock(withVoice), withVoice, withIn, b));

	EXPECT_EQ(withOut.directDownloadPort, withIn.directDownloadPort);
	EXPECT_EQ(withOut.iwadHash, withIn.iwadHash);
	EXPECT_EQ(a + 1, b) << "exactly one extra byte, and it must be accounted for";
}

// ---------------------------------------------------------------- the general property

TEST(LauncherFields, EveryFieldCombinationConsumesExactlyWhatWasWritten)
{
	// The test that catches the NEXT unhandled bit rather than only this one. If a field is added to
	// the writer and not the parser (or read at the wrong width), the byte count stops matching for
	// every combination containing it.
	for (unsigned flags = 0; flags <= kAllBits; ++flags)
	{
		if ((flags & ~kAllBits) != 0)
			continue;

		const vector<unsigned char> bytes = BuildBlock(flags);

		LauncherExtendedInfo info;
		size_t consumed = 0;
		ASSERT_EQ(ExtendedParse::Ok, Parse(bytes, flags, info, consumed))
			<< "flags " << flags;
		EXPECT_EQ(bytes.size(), consumed)
			<< "flags " << flags << ": parser and writer disagree about field widths";
	}
}

TEST(LauncherFields, EveryCombinationRecoversTheValuesItCarried)
{
	for (unsigned flags = 0; flags <= kAllBits; ++flags)
	{
		if ((flags & ~kAllBits) != 0)
			continue;

		LauncherExtendedInfo info;
		size_t consumed = 0;
		ASSERT_EQ(ExtendedParse::Ok, Parse(BuildBlock(flags), flags, info, consumed));

		if (flags & zx::kSqf2PwadHashes)
			EXPECT_EQ(2u, info.pwadHashes.size()) << "flags " << flags;
		if (flags & zx::kSqf2Country)
			EXPECT_EQ("XIP", info.countryCode) << "flags " << flags;
		if (flags & zx::kSqf2GameModeName)
			EXPECT_EQ("Cooperative", info.gameModeName) << "flags " << flags;
		if (flags & zx::kSqf2GameModeShortName)
			EXPECT_EQ("COOP", info.gameModeShortName) << "flags " << flags;
		if (flags & zx::kSqf2VoiceChat)
			EXPECT_EQ(1, info.voiceChat) << "flags " << flags;
		if (flags & zx::kSqf2DirectDownload)
			EXPECT_EQ(kPort, info.directDownloadPort) << "flags " << flags;
		if (flags & zx::kSqf2IwadHash)
			EXPECT_EQ(32u, info.iwadHash.size()) << "flags " << flags;
		if (flags & zx::kSqf2WadSizes)
		{
			ASSERT_EQ(2u, info.pwadSizes.size()) << "flags " << flags;
			EXPECT_EQ(14263296ULL, info.pwadSizes[0]) << "flags " << flags;

			// The one that would come back negative through a signed read, which is not a smaller
			// file -- it is a nonsense one, and it would print as garbage next to the filename.
			EXPECT_EQ(3221225472ULL, info.pwadSizes[1]) << "flags " << flags;
		}
	}
}

// ---------------------------------------------------------------- refusing rather than guessing

TEST(LauncherFields, RefusesABitItCannotConsume)
{
	// The structural fix. A variable-length field cannot be stepped over without knowing its width,
	// so meeting an unknown bit means we have already lost our place -- refuse the block instead of
	// returning confident nonsense.
	const unsigned unknown = 0x00000100;
	ASSERT_EQ(0u, KnownExtendedFields() & unknown);

	LauncherExtendedInfo info;
	size_t consumed = 0;
	EXPECT_EQ(ExtendedParse::UnknownField,
		Parse(BuildBlock(kAllBits), kAllBits | unknown, info, consumed));
	EXPECT_EQ(0u, consumed);
}

TEST(LauncherFields, KnownFieldsIsExactlyWhatTheParserHandles)
{
	// If a bit is added to the header without a parse, this fails rather than the field silently
	// becoming unreadable at runtime.
	EXPECT_EQ(kAllBits, KnownExtendedFields());
}

TEST(LauncherFields, RefusesEveryTruncationOfAFullBlock)
{
	// Covers the truncated path of every field at once: cut the block at each possible length and
	// none of them may report success.
	const vector<unsigned char> full = BuildBlock(kAllBits);

	for (size_t len = 0; len < full.size(); ++len)
	{
		vector<unsigned char> chopped(full.begin(), full.begin() + len);

		LauncherExtendedInfo info;
		size_t consumed = 0;
		EXPECT_EQ(ExtendedParse::Truncated, Parse(chopped, kAllBits, info, consumed))
			<< "accepted a block cut to " << len << " bytes";
	}
}

TEST(LauncherFields, RefusesAStringWithNoTerminator)
{
	Writer w;
	w.Raw("COOP");							// no NUL: the writer always emits one
	LauncherExtendedInfo info;
	size_t consumed = 0;
	EXPECT_EQ(ExtendedParse::Truncated,
		Parse(w.bytes, zx::kSqf2GameModeShortName, info, consumed));
}

// ---------------------------------------------------------------- values that used to break

TEST(LauncherFields, AcceptsAPortAboveThirtyTwoThousand)
{
	// BYTESTREAM_s::ReadShort casts to a SIGNED short, so 50000 comes back as -15536 -- and a guard
	// rejecting non-positive ports would refuse every server on a high port. A port is not signed.
	Writer w;
	w.Byte(0);
	w.Short(50000);

	LauncherExtendedInfo info;
	size_t consumed = 0;
	ASSERT_EQ(ExtendedParse::Ok, Parse(w.bytes, zx::kSqf2DirectDownload, info, consumed));
	EXPECT_EQ(50000, info.directDownloadPort);
}

TEST(LauncherFields, ReadsThePrefersMirrorsFlag)
{
	Writer w;
	w.Byte(1);
	w.Short(kPort);

	LauncherExtendedInfo info;
	size_t consumed = 0;
	ASSERT_EQ(ExtendedParse::Ok, Parse(w.bytes, zx::kSqf2DirectDownload, info, consumed));
	EXPECT_TRUE(info.prefersMirrors);
	EXPECT_EQ(kPort, info.directDownloadPort);
}

TEST(LauncherFields, CountryIsThreeRawBytesWithNoTerminator)
{
	// The one field here that is not self-delimiting, so a width mistake desynchronises everything
	// after it rather than being absorbed by the next string.
	const unsigned flags = zx::kSqf2Country | zx::kSqf2GameModeName;

	LauncherExtendedInfo info;
	size_t consumed = 0;
	ASSERT_EQ(ExtendedParse::Ok, Parse(BuildBlock(flags), flags, info, consumed));
	EXPECT_EQ("XIP", info.countryCode);
	EXPECT_EQ("Cooperative", info.gameModeName) << "a country width error lands here first";
}

TEST(LauncherFields, ReadsNoPwadHashesWhenTheCountIsZero)
{
	Writer w;
	w.Byte(0);

	LauncherExtendedInfo info;
	size_t consumed = 0;
	ASSERT_EQ(ExtendedParse::Ok, Parse(w.bytes, zx::kSqf2PwadHashes, info, consumed));
	EXPECT_TRUE(info.pwadHashes.empty());
	EXPECT_EQ(1u, consumed);
}

TEST(LauncherFields, AnEmptyBlockWithNoFlagsIsFine)
{
	LauncherExtendedInfo info;
	size_t consumed = 0;
	EXPECT_EQ(ExtendedParse::Ok, ParseExtendedInfo(NULL, 0, 0, info, consumed));
	EXPECT_EQ(0u, consumed);
}

// ---------------------------------------------------------------- the size field, byte by byte

TEST(LauncherFields, ReadsWadSizesLittleEndian)
{
	// Spelled out rather than round-tripped through the writer: if the writer and the parser shared a
	// byte-order mistake they would agree with each other and disagree with every real server.
	const unsigned char bytes[] = {
		0x02,								// two files
		0x00, 0xA4, 0xD9, 0x00,				// 0x00D9A400 = 14263296 -- doom2.wad
		0xFF, 0xFF, 0xFF, 0xFF,				// 4294967295 -- the top of the field
	};

	LauncherExtendedInfo info;
	size_t consumed = 0;
	ASSERT_EQ(ExtendedParse::Ok,
		ParseExtendedInfo(bytes, sizeof(bytes), zx::kSqf2WadSizes, info, consumed));

	EXPECT_EQ(sizeof(bytes), consumed);
	ASSERT_EQ(2u, info.pwadSizes.size());
	EXPECT_EQ(14263296ULL, info.pwadSizes[0]);

	// The one a signed read turns into -1, which is not a size at all.
	EXPECT_EQ(4294967295ULL, info.pwadSizes[1]);
}

TEST(LauncherFields, AServerWithNoPwadsStillWritesTheCountByte)
{
	// The field is present whenever it is asked for, because a field that is sometimes absent is what
	// desynchronises a stream -- the same rule SQF2_FUA_DIRECT_DOWNLOAD follows with port 0.
	const unsigned char bytes[] = { 0x00 };

	LauncherExtendedInfo info;
	size_t consumed = 0;
	ASSERT_EQ(ExtendedParse::Ok,
		ParseExtendedInfo(bytes, sizeof(bytes), zx::kSqf2WadSizes, info, consumed));

	EXPECT_EQ(1u, consumed);
	EXPECT_TRUE(info.pwadSizes.empty());
}

TEST(LauncherFields, RefusesASizeListCutShortOfItsCount)
{
	// A count byte promising four sizes with three and a half on the wire. Reading the half as a whole
	// would report a file some fraction of its real size, which is worse than reporting nothing.
	const unsigned char bytes[] = { 0x04, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00 };

	LauncherExtendedInfo info;
	size_t consumed = 0;
	EXPECT_EQ(ExtendedParse::Truncated,
		ParseExtendedInfo(bytes, sizeof(bytes), zx::kSqf2WadSizes, info, consumed));
	EXPECT_EQ(0u, consumed);
}

TEST(LauncherFields, TheSizeFieldSitsAfterTheIwadHashAndBeforeNothing)
{
	// Field ORDER is the whole risk with this protocol: it is ascending bit order, and the server's
	// dispatch table is a std::map keyed by bit value, so bit 7 is emitted last. Read it anywhere else
	// and the hash before it is consumed as sizes.
	const unsigned flags = zx::kSqf2IwadHash | zx::kSqf2WadSizes;

	LauncherExtendedInfo info;
	size_t consumed = 0;
	ASSERT_EQ(ExtendedParse::Ok, Parse(BuildBlock(flags), flags, info, consumed));

	EXPECT_EQ(32u, info.iwadHash.size());
	ASSERT_EQ(2u, info.pwadSizes.size());
	EXPECT_EQ(14263296ULL, info.pwadSizes[0]);
}
