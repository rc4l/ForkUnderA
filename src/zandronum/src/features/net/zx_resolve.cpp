// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "networkheaders.h"

#include "features/net/zx_resolve.h"
#include "features/net/computation/hostliteral_compute.h"
#include "networkshared.h"

#include <string.h>

namespace zx
{

bool ResolveHostV6( const char *pszHost, NETADDRESS_s &Out )
{
	if ( pszHost == NULL )
		return false;

	char szName[512];
	const HostLiteral literal = ClassifyHost( pszHost, szName, sizeof szName );

	// [rc4l] A dotted quad has no AAAA, and asking for one is not free: the string will not parse as
	// an IPv6 address, so the resolver treats it as a name and chases the search domains to NXDOMAIN
	// while the caller waits.
	if ( literal == HostLiteral::V4 )
		return false;

	struct sockaddr_in6 Address6;
	memset( &Address6, 0, sizeof Address6 );
	Address6.sin6_family = AF_INET6;

	if ( literal == HostLiteral::V6 )
	{
		if ( inet_pton( AF_INET6, szName, &Address6.sin6_addr ) != 1 )
			return false;

		Out.LoadFromSocketAddress( *reinterpret_cast<const struct sockaddr *>( &Address6 ));
		return true;
	}

	struct addrinfo hints;
	struct addrinfo *pResult = NULL;
	memset( &hints, 0, sizeof hints );
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;

	// [rc4l] Skip the lookup entirely on a machine with no IPv6 of its own, which is the same waste
	// the literal check above avoids and the far more common one.
	hints.ai_flags = AI_ADDRCONFIG;

	if (( getaddrinfo( szName, NULL, &hints, &pResult ) != 0 ) || ( pResult == NULL ))
		return false;

	// [rc4l] Refuse a v4-mapped answer, since asking for AF_INET6 with no AAAA record hands back
	// ::ffff:a.b.c.d and every dual-stack host would then announce twice over IPv4.
	bool bIsRealV6 = false;
	if ( pResult->ai_addr->sa_family == AF_INET6 )
	{
		const struct sockaddr_in6 *pAddr6 = reinterpret_cast<const struct sockaddr_in6 *>( pResult->ai_addr );
		bIsRealV6 = ( IN6_IS_ADDR_V4MAPPED( &pAddr6->sin6_addr ) == 0 );
	}

	if ( bIsRealV6 )
		Out.LoadFromSocketAddress( *pResult->ai_addr );

	freeaddrinfo( pResult );
	return bIsRealV6;
}

} // namespace zx
