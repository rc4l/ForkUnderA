// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net/computation/v6prefix_compute.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zx
{

namespace
{

// One hex group, or -1. Deliberately strict: anything that is not four-or-fewer hex digits is a
// broken rule, and a broken ban rule has to fail rather than approximate.
int ParseGroup( const char *text, size_t len )
{
	if (( len == 0 ) || ( len > 4 ))
		return -1;

	int value = 0;
	for ( size_t i = 0; i < len; ++i )
	{
		const char c = text[i];
		int digit;

		if (( c >= '0' ) && ( c <= '9' ))
			digit = c - '0';
		else if (( c >= 'a' ) && ( c <= 'f' ))
			digit = c - 'a' + 10;
		else if (( c >= 'A' ) && ( c <= 'F' ))
			digit = c - 'A' + 10;
		else
			return -1;

		value = ( value * 16 ) + digit;
	}

	return value;
}

} // namespace

bool V6AddressInPrefix( const unsigned char *address, const unsigned char *prefix, int bits )
{
	if (( address == 0 ) || ( prefix == 0 ))
		return false;

	// An out-of-range length is a broken rule. Failing to "matches nothing" rather than clamping
	// keeps a typo from silently becoming a wider ban than anybody wrote.
	if (( bits < 0 ) || ( bits > 128 ))
		return false;

	const int whole = bits / 8;
	const int spare = bits % 8;

	if ( whole > 0 )
	{
		if ( memcmp( address, prefix, whole ) != 0 )
			return false;
	}

	if ( spare == 0 )
		return true;

	// The partial byte: compare only the top `spare` bits of it and ignore the rest.
	const unsigned char mask = static_cast<unsigned char>( 0xff << ( 8 - spare ));
	return (( address[whole] & mask ) == ( prefix[whole] & mask ));
}

bool ParseV6Prefix( const char *text, unsigned char *prefix, int *bits )
{
	if (( text == 0 ) || ( prefix == 0 ) || ( bits == 0 ))
		return false;

	// [rc4l] AN ASTERISK IS A PREFIX SPELLED THE WAY PEOPLE ALREADY SPELL IT.
	//
	// The v4 list has always taken 1.2.3.*, so a v6 list that only took /64 would be asking everybody
	// to learn a second notation for the same idea. A v6 group is sixteen bits, so a star after N
	// groups is exactly a prefix of N*16 bits: 2001:db8:* is 2001:db8::/32, and there is no case
	// where the two disagree.
	//
	// Rewritten into the slash form rather than matched separately, so ONE matcher answers both and
	// a bug in prefix comparison cannot be a bug in only one of the two notations.
	char rewritten[80];
	const char *star = strchr( text, '*' );
	if ( star != 0 )
	{
		// Only as the last thing, and only whole groups. "2001:*:5" is not a range anybody means.
		if ( star[1] != 0 )
			return false;

		const size_t headLen = static_cast<size_t>( star - text );
		if ( headLen >= 48 )
			return false;

		char head[48];
		memcpy( head, text, headLen );
		head[headLen] = 0;

		// Count the groups actually written before the star.
		int groupCount = 0;
		for ( size_t i = 0; i < headLen; ++i )
		{
			if (( head[i] == ':' ) && ( i > 0 ) && ( head[i - 1] != ':' ))
				++groupCount;
		}

		// A bare "*" is everybody, which is never what somebody meant to type and is exactly the
		// mistake a ban list must not make quietly.
		if ( groupCount == 0 )
			return false;

		// Trim the trailing colon so "::" can be appended cleanly, then say the length outright.
		if (( headLen > 0 ) && ( head[headLen - 1] == ':' ))
			head[headLen - 1] = 0;

		snprintf( rewritten, sizeof( rewritten ), "%s::/%d", head, groupCount * 16 );
		text = rewritten;
	}

	const char *slash = strchr( text, '/' );
	if ( slash == 0 )
		return false;			// no length: see the header for why this is refused, not defaulted

	const int len = atoi( slash + 1 );
	if (( len < 0 ) || ( len > 128 ))
		return false;

	// A length needs at least one digit after the slash, and atoi cannot tell "0" from "nonsense".
	if (( slash[1] < '0' ) || ( slash[1] > '9' ))
		return false;

	const size_t addrLen = static_cast<size_t>( slash - text );
	if (( addrLen == 0 ) || ( addrLen >= 64 ))
		return false;

	char copy[64];
	memcpy( copy, text, addrLen );
	copy[addrLen] = 0;

	// Split on "::" once, which stands for the longest run of zero groups. More than one is
	// ambiguous and refused.
	const char *gap = strstr( copy, "::" );
	if (( gap != 0 ) && ( strstr( gap + 2, "::" ) != 0 ))
		return false;

	unsigned short groups[8];
	memset( groups, 0, sizeof( groups ));

	int head = 0;
	int tail = 0;
	unsigned short headGroups[8];
	unsigned short tailGroups[8];

	const char *cursor = copy;
	const char *stop = ( gap != 0 ) ? gap : ( copy + addrLen );

	// Groups before the "::", or all of them when there is none.
	while ( cursor < stop )
	{
		const char *colon = strchr( cursor, ':' );
		const char *end = (( colon != 0 ) && ( colon < stop )) ? colon : stop;

		const int value = ParseGroup( cursor, static_cast<size_t>( end - cursor ));
		if (( value < 0 ) || ( head >= 8 ))
			return false;

		headGroups[head++] = static_cast<unsigned short>( value );
		cursor = ( end < stop ) ? ( end + 1 ) : stop;
	}

	// Groups after it.
	if ( gap != 0 )
	{
		cursor = gap + 2;
		const char *tailStop = copy + addrLen;

		while ( cursor < tailStop )
		{
			const char *colon = strchr( cursor, ':' );
			const char *end = ( colon != 0 ) ? colon : tailStop;

			const int value = ParseGroup( cursor, static_cast<size_t>( end - cursor ));
			if (( value < 0 ) || ( tail >= 8 ))
				return false;

			tailGroups[tail++] = static_cast<unsigned short>( value );
			cursor = ( end < tailStop ) ? ( end + 1 ) : tailStop;
		}
	}

	if ( head + tail > 8 )
		return false;

	// Without a "::" every group has to be spelled out.
	if (( gap == 0 ) && ( head != 8 ))
		return false;

	for ( int i = 0; i < head; ++i )
		groups[i] = headGroups[i];
	for ( int i = 0; i < tail; ++i )
		groups[8 - tail + i] = tailGroups[i];

	for ( int i = 0; i < 8; ++i )
	{
		prefix[i * 2] = static_cast<unsigned char>( groups[i] >> 8 );
		prefix[i * 2 + 1] = static_cast<unsigned char>( groups[i] & 0xff );
	}

	*bits = len;
	return true;
}

//*****************************************************************************
//
bool FormatV6Prefix( const unsigned char *prefix, int bits, char *out, int outSize )
{
	if (( prefix == 0 ) || ( out == 0 ) || ( outSize < 48 ))
		return false;

	// [rc4l] A length outside 0..128 is a broken rule, and writing it back would make that permanent.
	if (( bits < 0 ) || ( bits > 128 ))
		return false;

	unsigned short groups[8];
	for ( int i = 0; i < 8; ++i )
		groups[i] = static_cast<unsigned short>(( prefix[i * 2] << 8 ) | prefix[i * 2 + 1] );

	// [rc4l] The longest zero run is the one "::" replaces, runs of one left alone per RFC 5952.
	int bestStart = -1, bestLen = 0;
	int runStart = -1, runLen = 0;
	for ( int i = 0; i < 8; ++i )
	{
		if ( groups[i] == 0 )
		{
			if ( runStart < 0 ) { runStart = i; runLen = 0; }
			runLen++;
			if ( runLen > bestLen ) { bestLen = runLen; bestStart = runStart; }
		}
		else
		{
			runStart = -1; runLen = 0;
		}
	}
	if ( bestLen < 2 ) { bestStart = -1; bestLen = 0; }

	// [rc4l] "::" carries both of its colons, since letting neighbours supply one produces "1:2:3:/48"
	// for a trailing run.
	char body[46];
	int written = 0;
	bool suppressSeparator = true;	// nothing precedes the first thing written
	body[0] = 0;

	for ( int i = 0; i < 8; )
	{
		if ( i == bestStart )
		{
			written += snprintf( body + written, sizeof( body ) - written, "::" );
			i += bestLen;
			suppressSeparator = true;
			continue;
		}

		if ( suppressSeparator == false )
			written += snprintf( body + written, sizeof( body ) - written, ":" );

		written += snprintf( body + written, sizeof( body ) - written, "%x", groups[i] );
		suppressSeparator = false;
		++i;
	}

	snprintf( out, outSize, "%s/%d", body, bits );
	return true;
}

} // namespace zx
