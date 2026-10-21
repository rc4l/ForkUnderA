// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/bytesize_compute.h"

namespace zx
{

namespace
{
const char *const kUnits[] = { "b", "kb", "mb", "gb", "tb" };
const int kLastUnit = 4;
} // namespace

std::string FormatByteSize( unsigned long long bytes )
{
	// Pick the largest unit the value still has a whole number of.
	unsigned long long divisor = 1;
	int unit = 0;
	while (( unit < kLastUnit ) && ( bytes >= divisor * 1024ULL ))
	{
		divisor *= 1024ULL;
		++unit;
	}

	const unsigned long long half = divisor / 2;

	// Adding half to round to nearest would wrap on a value within half a unit of the top of the
	// range. Nothing that large is a file, but a formatter that reports 16 exabytes as "0b" is worse
	// than one that reports it a rounding step low.
	unsigned long long whole = ( bytes > ( static_cast<unsigned long long>( -1 ) - half ))
		? ( bytes / divisor )
		: (( bytes + half ) / divisor );

	// Rounding up can carry past the unit it was measured in: 1048570 bytes is 1023.99 kb, and
	// "1024kb" defeats the point of having a unit at all.
	if (( whole >= 1024 ) && ( unit < kLastUnit ))
	{
		whole /= 1024;
		++unit;
	}

	// Written out by hand rather than through snprintf: this is called per WAD per frame, and a
	// number under four digits does not need a format parser to print.
	char digits[24];
	int at = static_cast<int>( sizeof( digits ));
	digits[--at] = 0;

	if ( whole == 0 )
	{
		digits[--at] = '0';
	}
	else
	{
		while (( whole > 0 ) && ( at > 0 ))
		{
			digits[--at] = static_cast<char>( '0' + ( whole % 10 ));
			whole /= 10;
		}
	}

	std::string out( &digits[at] );
	out += kUnits[unit];
	return out;
}

} // namespace zx
