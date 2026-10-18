// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "computation/saveversion_compute.h"

int ComputeSaveVersionVerdict(int version, int oldest, int newest)
{
	if (version < oldest) return kSaveVersionTooOld;
	if (version > newest) return kSaveVersionTooNew;
	return kSaveVersionReadable;
}
