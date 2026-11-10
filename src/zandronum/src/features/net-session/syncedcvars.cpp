// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/net-session/syncedcvars.h"

#include <vector>

#include "c_cvars.h"
#include "doomstat.h"
#include "sv_commands.h"
#include "features/net-session/netrole.h"

namespace zx { namespace net {

// [Doruk-Aksoy] Built on first use, since the registrars run during static initialisation.
static std::vector<FBaseCVar *> &Registered()
{
	static std::vector<FBaseCVar *> cvars;
	return cvars;
}

SyncedCVar::SyncedCVar(FBaseCVar &cvar)
{
	Registered().push_back(&cvar);
}

void SendSyncedCVar(FBaseCVar &cvar)
{
	if (IsServer() && gamestate != GS_STARTUP)
		SERVERCOMMANDS_SetCVar(cvar);
}

void SendSyncedCVarsTo(int client)
{
	for (FBaseCVar *cvar : Registered())
		SERVERCOMMANDS_SetCVar(*cvar, client, SVCF_ONLYTHISCLIENT);
}

}} // namespace zx::net
