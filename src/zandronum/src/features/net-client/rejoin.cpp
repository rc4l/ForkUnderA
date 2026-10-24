// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/rejoin.h"

#include "cl_main.h"
#include "d_player.h"
#include "doomstat.h"

#include "features/net-client/computation/rejoin_compute.h"

namespace {
bool g_rejoin = false;
}

namespace zx { namespace netclient {

void NoteMapNew() { g_rejoin = ComputeRejoinsAfterMapNew(playeringame[consoleplayer], players[consoleplayer].bSpectating); }

int ConnectFlags(int flags) { return ComputeConnectFlags(flags, CCF_STARTASSPECTATOR, g_rejoin); }

void ForgetRejoin() { g_rejoin = false; }

}} // namespace zx::netclient
