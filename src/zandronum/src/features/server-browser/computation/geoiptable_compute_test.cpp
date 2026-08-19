// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/geoiptable_compute.h"

#include <string>
#include <vector>

using zx::GeoCodeForIndex;
using zx::GeoLookupCodeIndex;
using zx::GeoLookupCodeIndexV6;
using zx::GeoTable;
using zx::ParseGeoTable;

namespace
{

void PutU16(std::vector<unsigned char> &out, unsigned int value)
{
	out.push_back(static_cast<unsigned char>(value & 0xFF));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

void PutU32(std::vector<unsigned char> &out, unsigned int value)
{
	for (int i = 0; i < 4; ++i)
		out.push_back(static_cast<unsigned char>((value >> (i * 8)) & 0xFF));
}

void PutVarint(std::vector<unsigned char> &out, unsigned long long value)
{
	while (value >= 0x80)
	{
		out.push_back(static_cast<unsigned char>((value & 0x7F) | 0x80));
		value >>= 7;
	}
	out.push_back(static_cast<unsigned char>(value & 0x7F));
}

struct Entry
{
	unsigned long long start;
	unsigned char code;
};

// Delta-code a run of entries the way the generator does.
void PutSection(std::vector<unsigned char> &out, const std::vector<Entry> &entries)
{
	PutU32(out, static_cast<unsigned int>(entries.size()));

	unsigned long long prev = 0;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		PutVarint(out, entries[i].start - prev);
		out.push_back(entries[i].code);
		prev = entries[i].start;
	}
}

std::vector<unsigned char> Header(unsigned int codeCount = 3)
{
	std::vector<unsigned char> out;
	const char magic[8] = { 'F', 'U', 'A', 'G', 'E', 'O', '2', '\0' };
	out.insert(out.end(), magic, magic + 8);

	PutU16(out, codeCount);

	const char codes[6] = { 'Z', 'Z', 'U', 'S', 'G', 'B' };
	out.insert(out.end(), codes, codes + (codeCount * 2 > 6 ? 6 : codeCount * 2));
	return out;
}

std::vector<Entry> V4Entries()
{
	std::vector<Entry> e;
	e.push_back({ 0x00000000ull, 0 });	// 0.0.0.0   unknown
	e.push_back({ 0x10000000ull, 1 });	// 16.0.0.0  US
	e.push_back({ 0x20000000ull, 2 });	// 32.0.0.0  GB
	e.push_back({ 0x30000000ull, 0 });	// 48.0.0.0  unknown again
	return e;
}

std::vector<Entry> V6Entries()
{
	std::vector<Entry> e;
	e.push_back({ 0x0000000000000000ull, 0 });
	e.push_back({ 0x2001'0db8'0000'0000ull, 1 });	// 2001:db8::/32   US
	e.push_back({ 0x2001'0db9'0000'0000ull, 2 });	// 2001:db9::/32   GB
	e.push_back({ 0x2001'0dba'0000'0000ull, 0 });
	return e;
}

// A table with three countries and four ranges in each family, matching what the generator emits.
std::vector<unsigned char> Build()
{
	std::vector<unsigned char> out = Header();
	PutSection(out, V4Entries());
	PutSection(out, V6Entries());
	return out;
}

unsigned int Ip(unsigned a, unsigned b, unsigned c, unsigned d)
{
	return (a << 24) | (b << 16) | (c << 8) | d;
}

// A 16-byte address from its top 64 bits; the bottom half is deliberately noise, because the table
// must not consult it.
std::vector<unsigned char> Ip6(unsigned long long top, unsigned char tail = 0x99)
{
	std::vector<unsigned char> out(16, tail);
	for (int i = 0; i < 8; ++i)
		out[i] = static_cast<unsigned char>((top >> (56 - i * 8)) & 0xFF);
	return out;
}

GeoTable Parsed(const std::vector<unsigned char> &raw)
{
	return ParseGeoTable(&raw[0], raw.size());
}

} // namespace

// ---------------------------------------------------------------- v4 lookup

TEST(GeoTable, FindsTheRangeAnAddressFallsIn)
{
	const GeoTable table = Parsed(Build());
	ASSERT_TRUE(table.valid);

	EXPECT_EQ(1u, GeoLookupCodeIndex(table, Ip(16, 0, 0, 0)));		// first address of the range
	EXPECT_EQ(1u, GeoLookupCodeIndex(table, Ip(24, 5, 5, 5)));		// somewhere inside it
	EXPECT_EQ(1u, GeoLookupCodeIndex(table, Ip(31, 255, 255, 255)));	// last address before the next
	EXPECT_EQ(2u, GeoLookupCodeIndex(table, Ip(32, 0, 0, 0)));		// and the next one over
}

TEST(GeoTable, AGapResolvesToUnknownRatherThanTheRangeBeforeIt)
{
	const GeoTable table = Parsed(Build());
	EXPECT_EQ(0u, GeoLookupCodeIndex(table, Ip(8, 0, 0, 0)));
	EXPECT_EQ(0u, GeoLookupCodeIndex(table, Ip(200, 0, 0, 0)));
}

TEST(GeoTable, AnAddressBelowEveryRangeIsUnknown)
{
	// The shipped table starts at 0 so this cannot arise from it, but a hand-made one need not be
	// so tidy, and the binary search must not read entry -1 to find out.
	std::vector<unsigned char> raw = Header();
	std::vector<Entry> e;
	e.push_back({ 0x10000000ull, 1 });
	PutSection(raw, e);
	PutSection(raw, V6Entries());

	const GeoTable table = Parsed(raw);
	ASSERT_TRUE(table.valid);
	EXPECT_EQ(0u, GeoLookupCodeIndex(table, Ip(1, 2, 3, 4)));
}

TEST(GeoTable, TheLastRangeRunsToTheEndOfTheAddressSpace)
{
	const GeoTable table = Parsed(Build());
	EXPECT_EQ(0u, GeoLookupCodeIndex(table, 0xFFFFFFFFu));
}

TEST(GeoTable, AnEntryNamingACountryTheTableDoesNotHaveReadsAsUnknown)
{
	std::vector<unsigned char> raw = Header();
	std::vector<Entry> e;
	e.push_back({ 0ull, 0 });
	e.push_back({ 0x10000000ull, 9 });		// only three codes exist
	PutSection(raw, e);
	PutSection(raw, V6Entries());

	const GeoTable table = Parsed(raw);
	ASSERT_TRUE(table.valid);
	EXPECT_EQ(0u, GeoLookupCodeIndex(table, Ip(16, 0, 0, 1)));
}

TEST(GeoTable, AnInvalidTableAnswersUnknownRatherThanReadingAnything)
{
	GeoTable empty;
	EXPECT_EQ(0u, GeoLookupCodeIndex(empty, Ip(8, 8, 8, 8)));

	const std::vector<unsigned char> ip = Ip6(0x2001'0db8'0000'0000ull);
	EXPECT_EQ(0u, GeoLookupCodeIndexV6(empty, &ip[0]));
}

// ---------------------------------------------------------------- v6 lookup

TEST(GeoTable, FindsTheV6RangeAnAddressFallsIn)
{
	const GeoTable table = Parsed(Build());
	ASSERT_TRUE(table.valid);

	const std::vector<unsigned char> first = Ip6(0x2001'0db8'0000'0000ull, 0x00);
	const std::vector<unsigned char> inside = Ip6(0x2001'0db8'1234'5678ull);
	const std::vector<unsigned char> last = Ip6(0x2001'0db8'ffff'ffffull);
	const std::vector<unsigned char> next = Ip6(0x2001'0db9'0000'0000ull);

	EXPECT_EQ(1u, GeoLookupCodeIndexV6(table, &first[0]));
	EXPECT_EQ(1u, GeoLookupCodeIndexV6(table, &inside[0]));
	EXPECT_EQ(1u, GeoLookupCodeIndexV6(table, &last[0]));
	EXPECT_EQ(2u, GeoLookupCodeIndexV6(table, &next[0]));
}

TEST(GeoTable, TheBottomHalfOfAV6AddressIsNeverConsulted)
{
	// The table is stored to /64, so two addresses sharing a prefix must answer identically however
	// far apart their host halves are -- the property that makes dropping those bytes safe.
	const GeoTable table = Parsed(Build());

	const std::vector<unsigned char> low = Ip6(0x2001'0db8'0000'0000ull, 0x00);
	const std::vector<unsigned char> high = Ip6(0x2001'0db8'0000'0000ull, 0xFF);

	EXPECT_EQ(GeoLookupCodeIndexV6(table, &low[0]), GeoLookupCodeIndexV6(table, &high[0]));
}

TEST(GeoTable, AV6AddressBelowEveryRangeIsUnknown)
{
	std::vector<unsigned char> raw = Header();
	PutSection(raw, V4Entries());
	std::vector<Entry> e;
	e.push_back({ 0x2001'0db8'0000'0000ull, 1 });
	PutSection(raw, e);

	const GeoTable table = Parsed(raw);
	ASSERT_TRUE(table.valid);

	const std::vector<unsigned char> ip = Ip6(0x0000'0000'0000'0001ull);
	EXPECT_EQ(0u, GeoLookupCodeIndexV6(table, &ip[0]));
}

TEST(GeoTable, AV6EntryNamingAMissingCountryReadsAsUnknown)
{
	std::vector<unsigned char> raw = Header();
	PutSection(raw, V4Entries());
	std::vector<Entry> e;
	e.push_back({ 0ull, 0 });
	e.push_back({ 0x2001'0db8'0000'0000ull, 9 });
	PutSection(raw, e);

	const GeoTable table = Parsed(raw);
	ASSERT_TRUE(table.valid);

	const std::vector<unsigned char> ip = Ip6(0x2001'0db8'0000'0000ull);
	EXPECT_EQ(0u, GeoLookupCodeIndexV6(table, &ip[0]));
}

TEST(GeoTable, ANullV6AddressIsRefusedRatherThanRead)
{
	const GeoTable table = Parsed(Build());
	EXPECT_EQ(0u, GeoLookupCodeIndexV6(table, 0));
}

TEST(GeoTable, TheTopmostV6AddressStillLandsInTheLastRange)
{
	const GeoTable table = Parsed(Build());
	const std::vector<unsigned char> ip = Ip6(0xffff'ffff'ffff'ffffull, 0xff);
	EXPECT_EQ(0u, GeoLookupCodeIndexV6(table, &ip[0]));
}

// ---------------------------------------------------------------- codes

TEST(GeoTable, GivesBackTheTwoLetterCode)
{
	const GeoTable table = Parsed(Build());
	char code[3] = { 'x', 'x', 'x' };

	ASSERT_TRUE(GeoCodeForIndex(table, 1, code));
	EXPECT_STREQ("US", code);

	ASSERT_TRUE(GeoCodeForIndex(table, 2, code));
	EXPECT_STREQ("GB", code);
}

TEST(GeoTable, IndexZeroIsUnknownAndHasNoCode)
{
	// Zero is the gap filler, not a country, so asking for its code must fail rather than hand back
	// the "ZZ" placeholder as if it were one.
	const GeoTable table = Parsed(Build());
	char code[3] = { 'x', 'x', 'x' };

	EXPECT_FALSE(GeoCodeForIndex(table, 0, code));
	EXPECT_STREQ("", code);
}

TEST(GeoTable, AnIndexPastTheEndHasNoCode)
{
	const GeoTable table = Parsed(Build());
	char code[3] = { 'x', 'x', 'x' };

	EXPECT_FALSE(GeoCodeForIndex(table, 3, code));
	EXPECT_STREQ("", code);
}

TEST(GeoTable, AskingAnInvalidTableForACodeFails)
{
	GeoTable empty;
	char code[3] = { 'x', 'x', 'x' };

	EXPECT_FALSE(GeoCodeForIndex(empty, 1, code));
	EXPECT_STREQ("", code);
}

TEST(GeoTable, ANullDestinationIsRefusedRatherThanWrittenTo)
{
	const GeoTable table = Parsed(Build());
	EXPECT_FALSE(GeoCodeForIndex(table, 1, 0));
}

// ---------------------------------------------------------------- parsing

TEST(GeoTable, ARealTableParses)
{
	const GeoTable table = Parsed(Build());
	ASSERT_TRUE(table.valid);
	EXPECT_EQ(3u, table.codeCount);
	EXPECT_EQ(4u, table.v4Start.size());
	EXPECT_EQ(4u, table.v6Start.size());
}

TEST(GeoTable, DeltasAccumulateBackIntoTheOriginalStarts)
{
	// The one thing the whole format rests on: what was written as gaps has to come back as
	// addresses, or every lookup is off by however much drifted.
	const GeoTable table = Parsed(Build());
	ASSERT_TRUE(table.valid);

	EXPECT_EQ(0x00000000u, table.v4Start[0]);
	EXPECT_EQ(0x10000000u, table.v4Start[1]);
	EXPECT_EQ(0x20000000u, table.v4Start[2]);
	EXPECT_EQ(0x30000000u, table.v4Start[3]);

	EXPECT_EQ(0x2001'0db8'0000'0000ull, table.v6Start[1]);
	EXPECT_EQ(0x2001'0dba'0000'0000ull, table.v6Start[3]);
}

TEST(GeoTable, RefusesSomethingThatIsNotOurFileAtAll)
{
	const std::string junk = "this is not a geoip table, it is a sentence";
	const GeoTable table = ParseGeoTable(reinterpret_cast<const unsigned char *>(junk.c_str()), junk.size());
	EXPECT_FALSE(table.valid);
}

TEST(GeoTable, RefusesTheOlderFormat)
{
	// Version 1 stored absolute starts, so reading it as deltas would produce a table that parses
	// and answers nonsense -- the worst of the possible failures, and why the magic carries a digit.
	std::vector<unsigned char> raw = Build();
	raw[6] = '1';

	const GeoTable table = Parsed(raw);
	EXPECT_FALSE(table.valid);
}

TEST(GeoTable, RefusesNothingAtAll)
{
	EXPECT_FALSE(ParseGeoTable(0, 0).valid);

	const unsigned char byte = 0;
	EXPECT_FALSE(ParseGeoTable(&byte, 0).valid);
	EXPECT_FALSE(ParseGeoTable(&byte, 1).valid);
}

TEST(GeoTable, RefusesAHeaderThatIsCutOff)
{
	// Every prefix of a real file, which is what a truncated download actually looks like. None may
	// parse, and more importantly none may read past the end getting there.
	const std::vector<unsigned char> full = Build();

	for (size_t n = 1; n < full.size(); ++n)
	{
		const std::vector<unsigned char> cut(full.begin(), full.begin() + n);
		EXPECT_FALSE(ParseGeoTable(&cut[0], cut.size()).valid) << "truncated to " << n;
	}
}

TEST(GeoTable, RefusesACountThatRunsPastTheEnd)
{
	// The hostile case: a count large enough that trusting it would walk the heap. It has to be
	// caught by comparing against the bytes left, not by multiplying into a total that can wrap.
	std::vector<unsigned char> raw = Header();
	PutU32(raw, 0xFFFFFFF0u);		// says four billion entries follow
	raw.push_back(0x00);
	raw.push_back(0x01);

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, RefusesACodeCountThatRunsPastTheEnd)
{
	std::vector<unsigned char> raw;
	const char magic[8] = { 'F', 'U', 'A', 'G', 'E', 'O', '2', '\0' };
	raw.insert(raw.end(), magic, magic + 8);
	PutU16(raw, 60000);				// no room for 120,000 bytes of codes
	raw.push_back('Z');
	raw.push_back('Z');

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, RefusesAVarintThatNeverEnds)
{
	// Ten continuation bytes is already more than 64 bits can hold, so an eleventh means the file is
	// lying and the decode must stop rather than run to the end of the buffer.
	std::vector<unsigned char> raw = Header();
	PutU32(raw, 1);
	for (int i = 0; i < 12; ++i)
		raw.push_back(0x80);
	raw.push_back(0x01);

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, RefusesAVarintThatRunsOffTheEnd)
{
	std::vector<unsigned char> raw = Header();
	PutU32(raw, 1);
	raw.push_back(0x80);			// continuation, then nothing

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, RefusesAnEntryWhoseCodeByteIsMissing)
{
	std::vector<unsigned char> raw = Header();
	PutU32(raw, 1);
	PutVarint(raw, 5);				// a start, but the file ends before its country

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, RefusesDeltasThatCarryPastTheEndOfTheV4Space)
{
	// A v4 start is 32 bits. Deltas that sum past that are corrupt, and truncating them would put
	// entries out of order, which silently breaks the binary search rather than failing.
	std::vector<unsigned char> raw = Header();
	PutU32(raw, 2);
	PutVarint(raw, 0xFFFFFFF0ull); raw.push_back(1);
	PutVarint(raw, 0xFFFFFFF0ull); raw.push_back(2);

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, RefusesDeltasThatCarryPastTheEndOfTheV6Space)
{
	std::vector<unsigned char> raw = Header();
	PutSection(raw, V4Entries());
	PutU32(raw, 2);
	PutVarint(raw, 0xFFFFFFFFFFFFFFF0ull); raw.push_back(1);
	PutVarint(raw, 0xFFFFFFFFFFFFFFF0ull); raw.push_back(2);

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, RefusesAFileThatStopsBeforeTheV6Section)
{
	std::vector<unsigned char> raw = Header();
	PutSection(raw, V4Entries());

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, RefusesATableWithNoRangesInEitherFamily)
{
	// Nothing to answer with. Reported as absent rather than as a table that says "unknown", so the
	// caller falls back instead of trusting it.
	std::vector<unsigned char> raw = Header();
	PutU32(raw, 0);
	PutU32(raw, 0);

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, RefusesATableWithNoCountriesInIt)
{
	std::vector<unsigned char> raw;
	const char magic[8] = { 'F', 'U', 'A', 'G', 'E', 'O', '2', '\0' };
	raw.insert(raw.end(), magic, magic + 8);
	PutU16(raw, 0);
	PutSection(raw, V4Entries());
	PutSection(raw, V6Entries());

	EXPECT_FALSE(Parsed(raw).valid);
}

TEST(GeoTable, AcceptsAFamilyThatIsEmptyAsLongAsTheOtherIsNot)
{
	// A generator run against a v4-only source is not corrupt, it just has nothing to say about v6.
	std::vector<unsigned char> raw = Header();
	PutSection(raw, V4Entries());
	PutU32(raw, 0);

	const GeoTable table = Parsed(raw);
	ASSERT_TRUE(table.valid);
	EXPECT_EQ(1u, GeoLookupCodeIndex(table, Ip(16, 0, 0, 1)));

	const std::vector<unsigned char> ip = Ip6(0x2001'0db8'0000'0000ull);
	EXPECT_EQ(0u, GeoLookupCodeIndexV6(table, &ip[0]));
}
