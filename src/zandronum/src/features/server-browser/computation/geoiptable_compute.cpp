// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "geoiptable_compute.h"

#include <cstring>

namespace zx
{

namespace
{

const char kMagic[8] = { 'F', 'U', 'A', 'G', 'E', 'O', '2', '\0' };

const size_t kCodeSize = 2;

// A cursor that can always be asked for more and always answers honestly. Every read checks first,
// so a truncated or hostile file ends the decode instead of walking off the buffer.
struct Reader
{
	const unsigned char *p;
	const unsigned char *end;
	bool ok;

	Reader(const unsigned char *data, size_t size) : p(data), end(data + size), ok(true) {}

	unsigned int U16()
	{
		if ((ok == false) || (end - p < 2))
			return (ok = false), 0u;

		const unsigned int v = static_cast<unsigned int>(p[0]) | (static_cast<unsigned int>(p[1]) << 8);
		p += 2;
		return v;
	}

	unsigned int U32()
	{
		if ((ok == false) || (end - p < 4))
			return (ok = false), 0u;

		const unsigned int v = static_cast<unsigned int>(p[0])
			| (static_cast<unsigned int>(p[1]) << 8)
			| (static_cast<unsigned int>(p[2]) << 16)
			| (static_cast<unsigned int>(p[3]) << 24);
		p += 4;
		return v;
	}

	unsigned char U8()
	{
		if ((ok == false) || (p == end))
			return (ok = false), static_cast<unsigned char>(0);

		return *p++;
	}

	// LEB128. Ten groups of seven bits is the most that can fit in 64, so an eleventh means the file
	// is lying about something and the decode stops rather than silently truncating a huge delta.
	unsigned long long Varint()
	{
		unsigned long long value = 0;
		int shift = 0;

		for (int i = 0; i < 10; ++i)
		{
			if ((ok == false) || (p == end))
				return (ok = false), 0ull;

			const unsigned char byte = *p++;
			value |= static_cast<unsigned long long>(byte & 0x7f) << shift;

			if ((byte & 0x80) == 0)
				return value;

			shift += 7;
		}

		return (ok = false), 0ull;
	}
};

} // namespace

GeoTable ParseGeoTable(const unsigned char *data, size_t size)
{
	GeoTable table;

	if ((data == 0) || (size < sizeof kMagic) || (memcmp(data, kMagic, sizeof kMagic) != 0))
		return table;

	Reader in(data + sizeof kMagic, size - sizeof kMagic);

	const unsigned int codeCount = in.U16();

	// Checked against the bytes actually left rather than multiplied into a total, so a hostile count
	// cannot wrap the arithmetic into a small number that passes.
	if ((in.ok == false) || (static_cast<size_t>(codeCount) > static_cast<size_t>(in.end - in.p) / kCodeSize))
		return table;

	table.codes.assign(reinterpret_cast<const char *>(in.p), static_cast<size_t>(codeCount) * kCodeSize);
	in.p += static_cast<size_t>(codeCount) * kCodeSize;

	// [rc4l] Each start is stored as the gap from the one before, so the running total IS the address
	// and a delta that carries it past the end of the space means the file is corrupt.
	const unsigned int v4Count = in.U32();
	if ((in.ok == false) || (static_cast<size_t>(v4Count) > static_cast<size_t>(in.end - in.p)))
		return table;

	unsigned long long cursor4 = 0;
	table.v4Start.reserve(v4Count);
	table.v4Code.reserve(v4Count);

	for (unsigned int i = 0; i < v4Count; ++i)
	{
		cursor4 += in.Varint();
		const unsigned char code = in.U8();

		if ((in.ok == false) || (cursor4 > 0xffffffffull))
			return GeoTable();

		table.v4Start.push_back(static_cast<unsigned int>(cursor4));
		table.v4Code.push_back(code);
	}

	const unsigned int v6Count = in.U32();
	if ((in.ok == false) || (static_cast<size_t>(v6Count) > static_cast<size_t>(in.end - in.p)))
		return table;

	unsigned long long cursor6 = 0;
	table.v6Start.reserve(v6Count);
	table.v6Code.reserve(v6Count);

	for (unsigned int i = 0; i < v6Count; ++i)
	{
		const unsigned long long delta = in.Varint();
		const unsigned char code = in.U8();

		if ((in.ok == false) || (delta > 0xffffffffffffffffull - cursor6))
			return GeoTable();

		cursor6 += delta;
		table.v6Start.push_back(cursor6);
		table.v6Code.push_back(code);
	}

	// A table with no ranges at all cannot answer anything; treat it as absent rather than as one
	// that says "unknown", so callers fall back instead of trusting it.
	if ((codeCount == 0) || (table.v4Start.empty() && table.v6Start.empty()))
		return GeoTable();

	table.codeCount = codeCount;
	table.valid = true;
	return table;
}

namespace
{

// The last entry starting at or before `key`, or -1 when `key` is below all of them. Shared so the
// two families cannot drift into two different off-by-ones.
template <typename T>
long long FindEntry(const std::vector<T> &starts, T key)
{
	if (starts.empty() || (key < starts[0]))
		return -1;

	size_t lo = 0;
	size_t hi = starts.size() - 1;

	// Invariant: starts[lo] <= key. Narrowing until lo == hi leaves the last entry that begins at or
	// before it, which is the one whose range contains it.
	while (lo < hi)
	{
		const size_t mid = lo + ((hi - lo + 1) / 2);	// rounds up, so lo always advances

		if (starts[mid] <= key)
			lo = mid;
		else
			hi = mid - 1;
	}

	return static_cast<long long>(lo);
}

} // namespace

unsigned int GeoLookupCodeIndex(const GeoTable &table, unsigned int ip)
{
	if (table.valid == false)
		return 0;

	const long long at = FindEntry(table.v4Start, ip);
	if (at < 0)
		return 0;

	const unsigned int index = table.v4Code[static_cast<size_t>(at)];

	// An entry naming a code the table does not carry is corrupt; unknown is the safe reading.
	return (index >= table.codeCount) ? 0u : index;
}

unsigned int GeoLookupCodeIndexV6(const GeoTable &table, const unsigned char *ip)
{
	if ((table.valid == false) || (ip == 0))
		return 0;

	unsigned long long key = 0;
	for (int i = 0; i < 8; ++i)
		key = (key << 8) | ip[i];

	const long long at = FindEntry(table.v6Start, key);
	if (at < 0)
		return 0;

	const unsigned int index = table.v6Code[static_cast<size_t>(at)];

	return (index >= table.codeCount) ? 0u : index;
}

bool GeoCodeForIndex(const GeoTable &table, unsigned int index, char *out)
{
	if (out == 0)
		return false;

	out[0] = '\0';

	if ((table.valid == false) || (index == 0) || (index >= table.codeCount))
		return false;

	out[0] = table.codes[static_cast<size_t>(index) * kCodeSize];
	out[1] = table.codes[(static_cast<size_t>(index) * kCodeSize) + 1];
	out[2] = '\0';
	return true;
}

} // namespace zx
