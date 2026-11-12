// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/inventoryselect_compute.h"

namespace zx {

bool ComputeSelectsInventory(bool held, bool onInventoryBar)
{
	return held && onInventoryBar;
}

int ComputeInventoryBarTics(bool showInventoryBar, int currentTics, int ticRate)
{
	return showInventoryBar ? 5 * ticRate : currentTics;
}

} // namespace zx
