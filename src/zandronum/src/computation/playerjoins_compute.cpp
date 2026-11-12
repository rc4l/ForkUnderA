// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/playerjoins_compute.h"

namespace zx { namespace playerjoins {

unsigned ComputePlayerJoinsEventTeam(bool teamValid, unsigned team, unsigned noTeam)
{
	return teamValid ? team : noTeam;
}

}} // namespace zx::playerjoins
