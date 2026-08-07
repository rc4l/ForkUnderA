// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/geoiptable_compute.h"

#include <string>
#include <vector>

using zx::GeoCodeForIndex;
using zx::GeoLookupCodeIndex;
using zx::GeoTable;
using zx::ParseGeoTable;

namespace
{

void PutU32(std::vector<unsigned char> &out, unsigned int value)
{
	out.push_back(static_cast<unsigned char>(value & 0xFF));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
	out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
	out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
}

// A table with three countries and four ranges, matching what the generator emits.
std::vector<unsigned char> Build()
{
	std::vector<unsigned char> out;
	const char magic[8] = { 'F', 'U', 'A', 'G', 'E', 'O', '1', '\0' };
	out.insert(out.end(), magic, magic + 8);

	PutU32(out, 4);					// entries
	out.push_back(3); out.push_back(0);		// codes: ZZ, US, GB

	const char codes[6] = { 'Z', 'Z', 'U', 'S', 'G', 'B' };
	out.insert(out.end(), codes, codes + 6);

	PutU32(out, 0);			out.push_back(0);	// 0.0.0.0    unknown
	PutU32(out, 0x10000000);	out.push_back(1);	// 16.0.0.0   US
	PutU32(out, 0x20000000);	out.push_back(2);	// 32.0.0.0   GB
	PutU32(out, 0x30000000);	out.push_back(0);	// 48.0.0.0   unknown again

	return out;
}

unsigned int Ip(unsigned a, unsigned b, unsigned c, unsigned d)
{
	return (a << 24) | (b << 16) | (c << 8) | d;
}

} // namespace

// ---------------------------------------------------------------- lookup

TEST(GeoTable, FindsTheRangeAnAddressFallsIn)
{
	const std::vector<unsigned char> raw = Build();
	const GeoTable table = ParseGeoTable(&raw[0], raw.size());
	ASSERT_TRUE(table.valid);

	char code[3];

	ASSERT_TRUE(GeoCodeForIndex(table, GeoLookupCodeIndex(table, Ip(20, 1, 2, 3)), code));
	EXPECT_EQ(std::string("US"), std::string(code));

	ASSERT_TRUE(GeoCodeForIndex(table, GeoLookupCodeIndex(table, Ip(40, 0, 0, 1)), code));
	EXPECT_EQ(std::string("GB"), std::string(code));
}

TEST(GeoTable, TheFirstAddressOfARangeBelongsToIt)
{
	// Off-by-one at a boundary is how a binary search usually fails, and it would put a whole
	// country's first block in its neighbour.
	const std::vector<unsigned char> raw = Build();
	const GeoTable table = ParseGeoTable(&raw[0], raw.size());

	EXPECT_EQ(1u, GeoLookupCodeIndex(table, 0x10000000));		// first US address
	EXPECT_EQ(0u, GeoLookupCodeIndex(table, 0x0FFFFFFF));		// one below it
	EXPECT_EQ(2u, GeoLookupCodeIndex(table, 0x20000000));		// first GB address
	EXPECT_EQ(1u, GeoLookupCodeIndex(table, 0x1FFFFFFF));		// last US address
}

TEST(GeoTable, TheLastRangeRunsToTheEndOfTheAddressSpace)
{
	const std::vector<unsigned char> raw = Build();
	const GeoTable table = ParseGeoTable(&raw[0], raw.size());

	EXPECT_EQ(0u, GeoLookupCodeIndex(table, 0xFFFFFFFF));
}

TEST(GeoTable, UnknownRangesReportNoCode)
{
	const std::vector<unsigned char> raw = Build();
	const GeoTable table = ParseGeoTable(&raw[0], raw.size());

	char code[3];
	EXPECT_FALSE(GeoCodeForIndex(table, GeoLookupCodeIndex(table, Ip(1, 2, 3, 4)), code));
	EXPECT_EQ(std::string(""), std::string(code));
}

// ---------------------------------------------------------------- refusing bad files

TEST(GeoTable, RejectsSomethingThatIsNotOurTable)
{
	std::vector<unsigned char> raw = Build();
	raw[0] = 'X';
	EXPECT_FALSE(ParseGeoTable(&raw[0], raw.size()).valid);
}

TEST(GeoTable, RejectsATruncatedFile)
{
	const std::vector<unsigned char> raw = Build();

	// Every prefix short of the whole thing must be refused, not read up to.
	for (size_t n = 0; n < raw.size(); ++n)
		EXPECT_FALSE(ParseGeoTable(&raw[0], n).valid) << "accepted a " << n << " byte file";
}

TEST(GeoTable, RejectsACountThatRunsPastTheEnd)
{
	// [rc4l] The one that matters. The count is read out of the file and then used to index, so a
	// file claiming more entries than it carries is the whole overrun in one field.
	std::vector<unsigned char> raw = Build();
	raw[8] = 0xFF;
	raw[9] = 0xFF;
	EXPECT_FALSE(ParseGeoTable(&raw[0], raw.size()).valid);
}

TEST(GeoTable, RejectsACountBigEnoughToWrapTheArithmetic)
{
	// A count near 2^32 multiplied by the entry size overflows 32 bits and can land on a small
	// number that passes a naive check. Nothing here multiplies, so this must still be refused.
	std::vector<unsigned char> raw = Build();
	raw[8] = 0xFF; raw[9] = 0xFF; raw[10] = 0xFF; raw[11] = 0xFF;
	EXPECT_FALSE(ParseGeoTable(&raw[0], raw.size()).valid);
}

TEST(GeoTable, RejectsACodeCountThatRunsPastTheEnd)
{
	std::vector<unsigned char> raw = Build();
	raw[12] = 0xFF;
	raw[13] = 0xFF;
	EXPECT_FALSE(ParseGeoTable(&raw[0], raw.size()).valid);
}

TEST(GeoTable, AnEmptyTableIsTreatedAsAbsent)
{
	// Zero ranges cannot answer anything. Reporting it valid would have callers trust "unknown"
	// instead of falling back to whatever else they have.
	std::vector<unsigned char> raw = Build();
	raw[8] = 0; raw[9] = 0; raw[10] = 0; raw[11] = 0;
	EXPECT_FALSE(ParseGeoTable(&raw[0], raw.size()).valid);
}

TEST(GeoTable, NoTableAnswersUnknownRatherThanCrashing)
{
	const GeoTable table = ParseGeoTable(0, 0);
	EXPECT_FALSE(table.valid);
	EXPECT_EQ(0u, GeoLookupCodeIndex(table, Ip(8, 8, 8, 8)));

	char code[3];
	EXPECT_FALSE(GeoCodeForIndex(table, 1, code));
}

TEST(GeoTable, ACorruptCodeIndexReadsAsUnknown)
{
	// The per-entry index is one byte from the file and is used to index the code array.
	std::vector<unsigned char> raw = Build();
	raw[raw.size() - 1] = 200;			// far beyond the three codes present
	const GeoTable table = ParseGeoTable(&raw[0], raw.size());
	ASSERT_TRUE(table.valid);

	EXPECT_EQ(0u, GeoLookupCodeIndex(table, 0x30000000));
}
