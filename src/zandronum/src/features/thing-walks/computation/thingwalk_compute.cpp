// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/thing-walks/computation/thingwalk_compute.h"

bool ComputeMoverPassesThings(int flags2)
{
	return (flags2 & ZX_MF2_THRUACTORS) != 0;
}
