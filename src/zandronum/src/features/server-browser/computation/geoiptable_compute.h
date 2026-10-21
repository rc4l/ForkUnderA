// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Reading the country table ZandroX ships, and finding an address in it.
//
// The GeoIP library has always been compiled in, and a database never was. On Linux it could pick up
// /usr/share/GeoIP/GeoIP.dat if the distribution happened to install one; everywhere else the lookup
// failed and every internet server drew "?" instead of a flag. MaxMind discontinued the legacy .dat
// format in 2019, so there was nothing to ship even in principle.
//
// So we ship our own, built by tools/gen_geoip_table.py and packed into zandronum.pk3. The parsing
// lives here, away from the engine, because it reads a length and a count out of a file and then
// indexes with them -- which is the shape of every buffer overrun ever written, and the one thing in
// this feature worth testing properly.
//
// THE FILE IS DELTA-CODED AND THE LOOKUP IS NOT.
//
// Version 1 stored an absolute 32-bit start per entry so the mapped bytes could be binary-searched
// where they lay. That is why it compressed so badly: 358k absolute addresses share almost no
// structure, and deflate got 1.79 MB down to only 995 KB. Sorted starts are nearly-sorted small
// numbers once you subtract the previous one, and a varint delta of that is what deflate is good at
// -- the same v4 data is 294 KB in version 2, and IPv6 costs about the same again rather than the
// megabyte its 348k ranges suggest.
//
// The cost is that a varint stream cannot be indexed, so this decodes once at load into sorted
// arrays and searches those. Disk and download shrink; the lookup is the same binary search it was;
// resident memory is what it always was, because that is what the arrays are.
//
// IPv6 IS STORED TO /64 AND NO FINER. Of 348,330 ranges in the current DB-IP file, 391 split below a
// /64 -- one in nine hundred, all of them tiny allocations inside a single country's block. Carrying
// 128 bits to keep those would double the table for a rounding error, so the bottom half of the
// address is dropped and those few ranges resolve to whoever owns the /64 around them.
//
// Header-pure by the features/ rules: no engine types.

#ifndef ZX_GEOIPTABLE_COMPUTE_H
#define ZX_GEOIPTABLE_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx
{

// A decoded table. Unlike version 1 this OWNS its arrays, because a delta stream has to be expanded
// before anything can search it.
struct GeoTable
{
	std::vector<unsigned int> v4Start;			// sorted, each entry runs until the next begins
	std::vector<unsigned char> v4Code;			// parallel to v4Start
	std::vector<unsigned long long> v6Start;	// sorted, the top 64 bits of the address
	std::vector<unsigned char> v6Code;			// parallel to v6Start
	std::string codes;							// codeCount * 2 chars, not terminated
	unsigned int codeCount;
	bool valid;

	GeoTable() : codeCount(0), valid(false) {}
};

// Decode the whole file. Returns valid == false for anything that does not add up, INCLUDING a
// declared count that runs past the end of the buffer, a varint that never terminates, and a delta
// chain that would wrap past the top of the address space -- the point being that everything
// downstream then searches freely.
GeoTable ParseGeoTable(const unsigned char *data, size_t size);

// The code index for `ip`, or 0 for unknown. A binary search for the last start that is <= ip.
unsigned int GeoLookupCodeIndex(const GeoTable &table, unsigned int ip);

// The code index for a 16-byte IPv6 address, or 0 for unknown. Only the top 8 bytes are consulted;
// see the header note on /64.
unsigned int GeoLookupCodeIndexV6(const GeoTable &table, const unsigned char *ip);

// The two-letter code for an index, written into `out` (three bytes: two letters and a terminator).
// Returns false for an index the table does not have, leaving `out` an empty string.
bool GeoCodeForIndex(const GeoTable &table, unsigned int index, char *out);

} // namespace zx

#endif // ZX_GEOIPTABLE_COMPUTE_H
