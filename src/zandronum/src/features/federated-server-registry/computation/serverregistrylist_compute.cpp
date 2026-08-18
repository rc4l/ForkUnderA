// [rc4l] See serverregistrylist_compute.h. Pure logic only — unit-tested at 100% line coverage
// off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/federated-server-registry/computation/serverregistrylist_compute.h"

namespace zx
{

namespace
{

bool IsSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string Trim( const std::string &s )
{
	std::string::size_type b = 0, e = s.size();
	while ( b < e && IsSpace( s[b] ) )
		++b;
	while ( e > b && IsSpace( s[e - 1] ) )
		--e;
	return s.substr( b, e - b );
}

std::string LowerASCII( const std::string &s )
{
	std::string out( s );
	for ( std::string::size_type i = 0; i < out.size(); ++i )
	{
		if ( out[i] >= 'A' && out[i] <= 'Z' )
			out[i] = static_cast<char>( out[i] - 'A' + 'a' );
	}
	return out;
}

// One dot-separated label of a hostname: letters, digits and hyphens only, never starting or ending
// with a hyphen. Deliberately strict -- this is what stops a CDN challenge page or an HTML error
// body from parsing: "<!DOCTYPE" and "<html>" are not valid labels, so such a fetch yields zero
// entries and the caller treats it as the failure it is.
bool IsValidLabel( const std::string &label )
{
	if ( label.empty( ) || label.size( ) > 63 )
		return false;
	if ( label[0] == '-' || label[label.size( ) - 1] == '-' )
		return false;

	for ( std::string::size_type i = 0; i < label.size( ); ++i )
	{
		const char c = label[i];
		const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
			|| ( c >= '0' && c <= '9' ) || ( c == '-' );
		if ( !ok )
			return false;
	}
	return true;
}

// [rc4l] An IPv6 literal, judged loosely: hex digits and colons, at least two colons so it cannot be
// confused with "name:port".
//
// Loose on purpose. This decides whether to HAND the text to a resolver, and the resolver is the
// thing that actually knows what a valid address is -- duplicating inet_pton here would give two
// answers to one question and eventually disagree.
bool LooksLikeV6Literal( const std::string &host )
{
	int colons = 0;

	for ( std::string::size_type i = 0; i < host.size( ); ++i )
	{
		const char c = host[i];

		if ( c == ':' )
		{
			colons++;
			continue;
		}

		const bool hex = (( c >= '0' ) && ( c <= '9' )) || (( c >= 'a' ) && ( c <= 'f' ))
			|| (( c >= 'A' ) && ( c <= 'F' ));
		if ( !hex )
			return false;
	}

	return ( colons >= 2 );
}

// One place that decides what a port is, so the bracketed and unbracketed forms cannot drift apart.
bool ParsePort( const std::string &digits, int &out )
{
	if ( digits.empty( ) || digits.size( ) > 5 )
		return false;

	int value = 0;
	for ( std::string::size_type i = 0; i < digits.size( ); ++i )
	{
		if ( digits[i] < '0' || digits[i] > '9' )
			return false;
		value = value * 10 + ( digits[i] - '0' );
	}

	if ( value < 1 || value > 65535 )
		return false;

	out = value;
	return true;
}

// Split "<host>[:port]". Returns false (skip the entry) on anything we cannot make sense of, rather
// than guessing -- a mistyped port silently becoming the default would be a confusing way to fail.
bool ParseHostPort( const std::string &token, ServerRegistryEntry &out )
{
	std::string host = token;
	int port = 0;

	// [rc4l] BRACKETS FIRST, because the split below looks for the last colon and a v6 address is
	// mostly colons. "[2001:db8::1]:15300" would split at the port colon and leave "[2001:db8::1]",
	// which then fails the hostname rules; a bare "2001:db8::1" would be cut at its own last colon
	// and turn into a different address entirely. Both simply vanished from the list.
	if ( !token.empty( ) && ( token[0] == '[' ))
	{
		const std::string::size_type close = token.find( ']' );
		if ( close == std::string::npos )
			return false;

		host = token.substr( 1, close - 1 );

		const std::string rest = token.substr( close + 1 );
		if ( !rest.empty( ) )
		{
			if ( rest[0] != ':' )
				return false;

			if ( !ParsePort( rest.substr( 1 ), port ) )
				return false;
		}

		if ( !LooksLikeV6Literal( host ) )
			return false;

		out.host = host;
		out.port = port;
		return true;
	}

	// An unbracketed v6 literal carries no port, since there would be no way to tell one from the
	// address's own last group.
	if ( LooksLikeV6Literal( token ) )
	{
		out.host = token;
		out.port = 0;
		return true;
	}

	const std::string::size_type colon = token.rfind( ':' );

	if ( colon != std::string::npos )
	{
		host = token.substr( 0, colon );

		if ( !ParsePort( token.substr( colon + 1 ), port ) )
			return false;
	}

	if ( !IsValidServerRegistryHost( host ) )
		return false;

	out.host = host;
	out.port = port;
	out.name.clear( );
	return true;
}

// Keep the FIRST occurrence of a host. Both callers rely on this: a repeated line in the fetched
// file must not double the browser's work, and a user entry must win over the shipped one.
void AppendUnique( std::vector<ServerRegistryEntry> &list, const ServerRegistryEntry &entry )
{
	const std::string key = LowerASCII( entry.host );
	for ( std::vector<ServerRegistryEntry>::size_type i = 0; i < list.size( ); ++i )
	{
		if ( LowerASCII( list[i].host ) == key )
			return;
	}
	list.push_back( entry );
}

} // namespace

bool IsValidServerRegistryHost( const std::string &host )
{
	// 253 is the DNS limit on a presentation-format name; anything longer cannot resolve anyway.
	if ( host.empty( ) || host.size( ) > 253 )
		return false;

	// A v6 literal is not a DNS name and must not be walked label by label -- every colon would fail
	// the label rules and the address would be rejected as a malformed hostname.
	if ( LooksLikeV6Literal( host ) )
		return true;

	std::string::size_type start = 0;
	for ( ;; )
	{
		const std::string::size_type dot = host.find( '.', start );
		const std::string label = ( dot == std::string::npos )
			? host.substr( start )
			: host.substr( start, dot - start );

		if ( !IsValidLabel( label ) )
			return false;

		if ( dot == std::string::npos )
			return true;
		start = dot + 1;
	}
}

std::vector<ServerRegistryEntry> ParseServerRegistryList( const std::string &text )
{
	std::vector<ServerRegistryEntry> out;

	// A UTF-8 BOM would otherwise glue itself to the first host and cost us that entry. Editors add
	// them without asking, and the first line is the one we can least afford to lose.
	std::string::size_type pos = 0;
	if ( text.size( ) >= 3 && text.compare( 0, 3, "\xEF\xBB\xBF" ) == 0 )
		pos = 3;

	while ( pos <= text.size( ) )
	{
		const std::string::size_type nl = text.find( '\n', pos );
		const std::string raw = ( nl == std::string::npos )
			? text.substr( pos )
			: text.substr( pos, nl - pos );

		const std::string line = Trim( raw );

		// Blank lines and whole-line comments only: '#' mid-line is left alone so a display name may
		// contain one.
		if ( !line.empty( ) && line[0] != '#' )
		{
			// First whitespace run splits "<host>[:port]" from the display name.
			std::string::size_type split = 0;
			while ( split < line.size( ) && !IsSpace( line[split] ) )
				++split;

			ServerRegistryEntry entry;
			if ( ParseHostPort( line.substr( 0, split ), entry ) )
			{
				entry.name = Trim( line.substr( split ) );
				AppendUnique( out, entry );
			}
		}

		if ( nl == std::string::npos )
			break;
		pos = nl + 1;
	}

	return out;
}

std::vector<ServerRegistryEntry> ParseServerRegistryCSV( const std::string &csv )
{
	std::vector<ServerRegistryEntry> out;

	std::string::size_type pos = 0;
	for ( ;; )
	{
		const std::string::size_type comma = csv.find( ',', pos );
		const std::string token = Trim( ( comma == std::string::npos )
			? csv.substr( pos )
			: csv.substr( pos, comma - pos ) );

		// Empty fields are normal in hand-typed input ("a.net,, b.net" or a trailing comma), so they
		// are skipped rather than treated as an error.
		ServerRegistryEntry entry;
		if ( !token.empty( ) && ParseHostPort( token, entry ) )
			AppendUnique( out, entry );

		if ( comma == std::string::npos )
			break;
		pos = comma + 1;
	}

	return out;
}

std::vector<ServerRegistryEntry> MergeServerRegistryLists( const std::vector<ServerRegistryEntry> &user,
                                                           const std::vector<ServerRegistryEntry> &shipped )
{
	std::vector<ServerRegistryEntry> out;
	for ( std::vector<ServerRegistryEntry>::size_type i = 0; i < user.size( ); ++i )
		AppendUnique( out, user[i] );
	for ( std::vector<ServerRegistryEntry>::size_type i = 0; i < shipped.size( ); ++i )
		AppendUnique( out, shipped[i] );
	return out;
}

} // namespace zx
