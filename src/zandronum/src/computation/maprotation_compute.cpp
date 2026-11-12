// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/maprotation_compute.h"

namespace zx { namespace maprotation {

bool ComputeSetsMapUsedStatus(bool inClientMode, unsigned position, unsigned numEntries, bool used, bool currentlyUsed)
{
	return !inClientMode && position < numEntries && used != currentlyUsed;
}

}} // namespace zx::maprotation
