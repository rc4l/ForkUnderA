// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-replication/rotation.h"
#include "features/net-session/netrole.h"
#include "sv_commands.h"

namespace zx { namespace netreplication {

void SendMapUsedStatus(unsigned position)
{
	if (zx::net::IsServer())
		SERVERCOMMANDS_SetMapUsedStatus(position);
}

}} // namespace zx::netreplication
