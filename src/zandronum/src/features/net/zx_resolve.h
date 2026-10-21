// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Resolving a host to an IPv6 address, which NETADDRESS_s::LoadFromString cannot do because it
// asks gethostbyname for an A record and nothing else.

#ifndef ZX_RESOLVE_H
#define ZX_RESOLVE_H

struct NETADDRESS_s;

namespace zx
{

// [rc4l] The AAAA for `host`, with any ":port" suffix ignored, false when there is none.
//
// [rc4l] A v4-mapped answer is refused: asking for AF_INET6 where no AAAA exists hands back
// ::ffff:a.b.c.d, which sends as IPv4 and would make a caller believe it had reached IPv6.
bool ResolveHostV6( const char *host, NETADDRESS_s &out );

} // namespace zx

#endif // ZX_RESOLVE_H
