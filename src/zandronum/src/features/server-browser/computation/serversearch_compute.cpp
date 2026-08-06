// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/serversearch_compute.h"
#include "features/server-browser/computation/serversort_compute.h"

namespace zx
{

std::string SearchKey( const std::string &query )
{
	std::string out;
	out.reserve( query.size( ));

	for ( size_t i = 0; i < query.size( ); ++i )
	{
		char c = query[i];
		if (( c >= 'A' ) && ( c <= 'Z' ))
			c = static_cast<char>( c - 'A' + 'a' );
		out.push_back( c );
	}

	return out;
}

bool ServerMatchesSearch( const std::string &name, const std::string &key )
{
	// An empty box is the absence of a filter, not a filter that excludes everything.
	if ( key.empty( ))
		return true;

	// ServerSortKey is exactly the right transform and already exists: colour codes stripped, folded
	// to lowercase. Reusing it also means searching and sorting can never disagree about what a
	// server is called.
	return ServerSortKey( name ).find( key ) != std::string::npos;
}

} // namespace zx
