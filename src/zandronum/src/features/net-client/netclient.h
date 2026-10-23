// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] net-client's public header: what a client has applied from its server, for what draws from its world.

#ifndef ZX_NETCLIENT_H
#define ZX_NETCLIENT_H

namespace zx { namespace netclient {

// [rc4l] A server packet applied (CLIENT_ParsePacket), the one thing that changes a client's world between its tics.
void NoteParsedPacket();

// [rc4l] How many server packets this process has applied since it started.
unsigned ParsedPackets();

}} // namespace zx::netclient

#endif // ZX_NETCLIENT_H
