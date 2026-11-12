// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/startitem_compute.h"

namespace zx {

bool ComputeStartItemMorphedPlayer(bool pickedUp, bool takenByThePlayer)
{
	return pickedUp && !takenByThePlayer;
}

} // namespace zx
