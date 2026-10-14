// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/rendersettings/computation/forcedvideo_compute.h"

namespace zx { namespace rendersettings {

int ComputeForcedVideoInt(bool forced, int value, int def)
{
	return forced ? def : value;
}

int ComputeForcedFogMode(bool forced, int mode)
{
	return forced && mode == 0 ? 1 : mode;
}

}} // namespace zx::rendersettings
