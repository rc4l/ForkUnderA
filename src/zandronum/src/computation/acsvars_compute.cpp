// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/acsvars_compute.h"

namespace zx {

bool ComputeNewGameClearsAcsVars(bool inClientMode, bool clientActive)
{
	return !inClientMode || !clientActive;
}

} // namespace zx
