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

#ifndef ZX_GEOIPTABLE_COMPUTE_H
#define ZX_GEOIPTABLE_COMPUTE_H

#include <cstddef>

namespace zx
{

// A parsed view over a buffer somebody else owns. Nothing here copies or frees.
struct GeoTable
{
	const unsigned char *entries;	// count * 5 bytes: little-endian u32 start, then u8 code index
	const char *codes;				// codeCount * 2 chars, not terminated
	unsigned int count;
	unsigned int codeCount;
	bool valid;
};

// Parse the header. Returns valid == false for anything that does not add up, INCLUDING a file whose
// declared counts run past the end of the buffer -- the point being that everything downstream may
// then index freely.
GeoTable ParseGeoTable(const unsigned char *data, size_t size);

// The code index for `ip`, or 0 for unknown. Entries are sorted by start address and each one runs
// until the next begins, so this is a binary search for the last start that is <= ip.
unsigned int GeoLookupCodeIndex(const GeoTable &table, unsigned int ip);

// The two-letter code for an index, written into `out` (three bytes: two letters and a terminator).
// Returns false for an index the table does not have, leaving `out` an empty string.
bool GeoCodeForIndex(const GeoTable &table, unsigned int index, char *out);

} // namespace zx

#endif // ZX_GEOIPTABLE_COMPUTE_H
