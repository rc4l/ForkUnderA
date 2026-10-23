// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-protocol/testfaults.h"
#include "features/net-protocol/computation/commandcapture_compute.h"
#include "features/net-protocol/computation/testfaults_compute.h"

#include "c_cvars.h"
#include "doomdef.h"   // [rc4l] TICRATE
#include "i_system.h"   // [rc4l] I_Sleep
#include "network_enums.h"   // [rc4l] SVC_EXTENDEDCOMMAND

// [rc4l] Server commands left unsent, as "id" or "id/extended id" (fuactl netcap names them), to prove a netgame scenario fails without them.
CVAR(String, fua_debug_netdrop, "", 0)
// [rc4l] Tics a client takes off the server tic it reports, as a screen that far behind would, to prove unlaggedhit fails.
CVAR(Int, fua_debug_unlagged_ticoffset, 0, 0)

// [rc4l] A server's tick stalls this many ms once a second, so a scenario sees what a slow tick does to ping and to its clients.
CVAR(Int, fua_debug_server_stall_ms, 0, 0)

namespace zx { namespace netprotocol {

bool DroppedForTest(const unsigned char *bytes, int length)
{
	const char *spec = fua_debug_netdrop;
	if (spec == nullptr || spec[0] == '\0') return false;
	int id, ext;
	ComputeCommandIds(bytes, length, SVC_EXTENDEDCOMMAND, id, ext);
	return ComputeNetDropMatches(spec, id, ext);
}

void StallServerForTest(int tic)
{
	if (const int ms = ComputeServerStallMs(tic, TICRATE, fua_debug_server_stall_ms)) I_Sleep(ms);
}

int ReportedServerTic(int held)
{
	return ComputeReportedServerTic(held, fua_debug_unlagged_ticoffset);
}

}} // namespace zx::netprotocol
