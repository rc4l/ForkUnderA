// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/statuschange_compute.h"

namespace zx {

bool ComputeMoveClearsStatuses(int lastStatusChangeTick, int gametic)
{
	return lastStatusChangeTick != gametic;
}

} // namespace zx
