// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "mapselect_compute.h"

namespace zx
{

bool ComputeSelectAllChanges(int inCount, int total)
{
	if (total <= 0)
		return false;

	// Anything not already in is something this would put in.
	return (inCount < total);
}

bool ComputeDeselectAllChanges(int inCount, int total)
{
	if (total <= 0)
		return false;

	return (inCount > 0);
}

} // namespace zx
