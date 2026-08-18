// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "networkheaders.h"

#include "features/net/zx_resolve.h"
#include "networkshared.h"

#include <string.h>

namespace zx
{

bool ResolveHostV6( const char *pszHost, NETADDRESS_s &Out )
{
	char szName[512];
	strncpy( szName, pszHost, sizeof szName - 1 );
	szName[sizeof szName - 1] = 0;

	// Strip a trailing :port exactly as LoadFromString does; a bare hostname has no colons.
	char *pszColon = strchr( szName, ':' );
	if ( pszColon != NULL )
		*pszColon = 0;

	struct addrinfo hints;
	struct addrinfo *pResult = NULL;
	memset( &hints, 0, sizeof hints );
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;

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
