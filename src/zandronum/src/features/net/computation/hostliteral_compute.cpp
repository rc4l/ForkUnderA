// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net/computation/hostliteral_compute.h"

#include <cstring>

namespace zx
{

namespace
{

// A dotted quad and nothing else: four decimal fields, each 0 to 255, no leading zeroes worth
// arguing about and no trailing rubbish.
bool IsDottedQuad( const char *text )
{
	int fields = 0;

	while ( *text != 0 )
	{
		int value = 0;
		int digits = 0;

		while (( *text >= '0' ) && ( *text <= '9' ))
		{
			value = ( value * 10 ) + ( *text - '0' );
			if ( ++digits > 3 )
				return false;
			++text;
		}

		if (( digits == 0 ) || ( value > 255 ))
			return false;

		++fields;

		if ( *text == '.' )
			++text;
		else if ( *text != 0 )
			return false;
	}

	return ( fields == 4 );
}

} // namespace

HostLiteral ClassifyHost( const char *host, char *out, size_t outSize )
{
	if (( host == 0 ) || ( out == 0 ) || ( outSize == 0 ))
		return HostLiteral::Name;

	out[0] = 0;

	const size_t length = strlen( host );
	if (( length == 0 ) || ( length >= outSize ))
		return HostLiteral::Name;

	// [rc4l] Brackets are the one spelling where the colons are the address rather than a port
	// separator, so they settle the question before anything below counts them.
	if ( host[0] == '[' )
	{
		const char *close = strchr( host, ']' );
		if ( close == 0 )
			return HostLiteral::Name;

		const size_t inner = static_cast<size_t>( close - host ) - 1;
		if (( inner == 0 ) || ( inner >= outSize ))
			return HostLiteral::Name;

		memcpy( out, host + 1, inner );
		out[inner] = 0;
		return HostLiteral::V6;
	}

	// Two or more colons cannot be a port, so the whole string is a bare v6 literal.
	const char *firstColon = strchr( host, ':' );
	if (( firstColon != 0 ) && ( strchr( firstColon + 1, ':' ) != 0 ))
	{
		memcpy( out, host, length );
		out[length] = 0;
		return HostLiteral::V6;
	}

	// One colon is a port, on a name or a dotted quad alike.
	const size_t keep = ( firstColon != 0 ) ? static_cast<size_t>( firstColon - host ) : length;
	if ( keep == 0 )
		return HostLiteral::Name;

	memcpy( out, host, keep );
	out[keep] = 0;

	return IsDottedQuad( out ) ? HostLiteral::V4 : HostLiteral::Name;
}

} // namespace zx
