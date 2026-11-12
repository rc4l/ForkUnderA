// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/wadinfo_compute.h"

namespace zx { namespace wadinfo {

bool ComputeIndexInRange(int index, unsigned count)
{
	return static_cast<unsigned>(index) < count;
}

int ComputeParentWadInfo(int parentWadNum, int wadNum)
{
	return parentWadNum != wadNum ? parentWadNum : -1;
}

}} // namespace zx::wadinfo
