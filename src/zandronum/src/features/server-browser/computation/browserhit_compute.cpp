// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/browserhit_compute.h"

namespace zx
{

int ComputeServerAtSlot(int slot, int visibleRows, int scrollFirst, int totalServers)
{
	if (slot < 0 || slot >= visibleRows)
		return -1;

	// A negative scroll offset is nonsense rather than something to clamp: silently treating it as 0
	// would map a corrupt offset onto real servers and join one of them.
	if (scrollFirst < 0 || totalServers <= 0)
		return -1;

	const int index = scrollFirst + slot;
	if (index >= totalServers)
		return -1;			// the blank tail of a partly-filled last page

	return index;
}

} // namespace zx
