// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/acsautomap_compute.h"

namespace zx {

int ComputeAutomapStatus(bool automapActive, bool viewActive)
{
	if (!automapActive)
		return AUTOMAP_OFF;
	return viewActive ? AUTOMAP_OVERLAYMAP : AUTOMAP_FULLMAP;
}

} // namespace zx
