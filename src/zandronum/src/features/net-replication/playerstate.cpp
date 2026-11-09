// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B

#include "features/net-replication/playerstate.h"

#include "sv_commands.h"

#include <set>

namespace zx { namespace netreplication {

void SendUserCVar(int playernum, FName name)
{
	std::set<FName> names;
	names.insert(name);
	SERVERCOMMANDS_SetPlayerUserInfo(playernum, names);
}

}} // namespace zx::netreplication
