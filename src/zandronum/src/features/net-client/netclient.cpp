// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/netclient.h"

namespace {
unsigned g_parsed = 0;
}

namespace zx { namespace netclient {

void NoteParsedPacket() { g_parsed++; }
unsigned ParsedPackets() { return g_parsed; }

}} // namespace zx::netclient
