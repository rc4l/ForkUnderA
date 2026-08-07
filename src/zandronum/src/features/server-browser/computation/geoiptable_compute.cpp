// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "geoiptable_compute.h"

#include <cstring>

namespace zx
{

namespace
{

const char kMagic[8] = { 'F', 'U', 'A', 'G', 'E', 'O', '1', '\0' };

const size_t kHeaderSize = 8 + 4 + 2;		// magic, u32 count, u16 codeCount
const size_t kEntrySize = 5;				// u32 start, u8 code index
const size_t kCodeSize = 2;

unsigned int ReadU32(const unsigned char *p)
{
	return static_cast<unsigned int>(p[0])
		| (static_cast<unsigned int>(p[1]) << 8)
		| (static_cast<unsigned int>(p[2]) << 16)
		| (static_cast<unsigned int>(p[3]) << 24);
}

unsigned int ReadU16(const unsigned char *p)
{
	return static_cast<unsigned int>(p[0]) | (static_cast<unsigned int>(p[1]) << 8);
}

GeoTable Invalid()
{
	GeoTable table;
	table.entries = 0;
	table.codes = 0;
	table.count = 0;
	table.codeCount = 0;
	table.valid = false;
	return table;
}

} // namespace

GeoTable ParseGeoTable(const unsigned char *data, size_t size)
{
	if ((data == 0) || (size < kHeaderSize))
		return Invalid();

	if (memcmp(data, kMagic, sizeof kMagic) != 0)
		return Invalid();

	const unsigned int count = ReadU32(data + 8);
	const unsigned int codeCount = ReadU16(data + 12);

	// [rc4l] The counts come out of the file, so they are checked against the file's actual length
	// before anything is allowed to index with them. Computed in size_t and compared against the
	// remaining bytes rather than multiplied into a total, so a hostile count cannot wrap the
	// arithmetic and produce a small number that passes.
	const size_t afterHeader = size - kHeaderSize;

	if (static_cast<size_t>(codeCount) > afterHeader / kCodeSize)
		return Invalid();

	const size_t afterCodes = afterHeader - (static_cast<size_t>(codeCount) * kCodeSize);

	if (static_cast<size_t>(count) > afterCodes / kEntrySize)
		return Invalid();

	// A table with no ranges cannot answer anything; treat it as absent rather than as a table that
	// says "unknown", so callers fall back instead of trusting it.
	if ((count == 0) || (codeCount == 0))
		return Invalid();

	GeoTable table;
	table.codes = reinterpret_cast<const char *>(data + kHeaderSize);
	table.entries = data + kHeaderSize + (static_cast<size_t>(codeCount) * kCodeSize);
	table.count = count;
	table.codeCount = codeCount;
	table.valid = true;
	return table;
}

unsigned int GeoLookupCodeIndex(const GeoTable &table, unsigned int ip)
{
	if (table.valid == false)
		return 0;

	// Below the first range start there is nothing to say. The generator emits an entry at 0, so in
	// the shipped table this cannot happen, but a hand-made one need not be so tidy.
	if (ip < ReadU32(table.entries))
		return 0;

	unsigned int lo = 0;
	unsigned int hi = table.count - 1;

	// Invariant: entry[lo].start <= ip. Narrowing until lo == hi leaves the last entry that starts
	// at or before ip, which is the one whose range contains it.
	while (lo < hi)
	{
		const unsigned int mid = lo + ((hi - lo + 1) / 2);	// rounds up, so lo always advances

		if (ReadU32(table.entries + (static_cast<size_t>(mid) * kEntrySize)) <= ip)
			lo = mid;
		else
			hi = mid - 1;
	}

	const unsigned int index = table.entries[(static_cast<size_t>(lo) * kEntrySize) + 4];

	// An entry naming a code the table does not carry is corrupt; unknown is the safe reading.
	if (index >= table.codeCount)
		return 0;

	return index;
}

bool GeoCodeForIndex(const GeoTable &table, unsigned int index, char *out)
{
	if (out == 0)
		return false;

	out[0] = '\0';

	if ((table.valid == false) || (index == 0) || (index >= table.codeCount))
		return false;

	out[0] = table.codes[(static_cast<size_t>(index) * kCodeSize)];
	out[1] = table.codes[(static_cast<size_t>(index) * kCodeSize) + 1];
	out[2] = '\0';
	return true;
}

} // namespace zx
