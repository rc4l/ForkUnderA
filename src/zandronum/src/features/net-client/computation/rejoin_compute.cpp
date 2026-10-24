// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/computation/rejoin_compute.h"

namespace zx { namespace netclient {

bool ComputeRejoinsAfterMapNew(bool inGame, bool spectating) { return inGame && !spectating; }

int ComputeConnectFlags(int flags, int startAsSpectatorBit, bool rejoin) { return rejoin ? (flags & ~startAsSpectatorBit) : flags; }

}} // namespace zx::netclient
