// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Implementation of the music volume checks; no engine dependencies.
#include "features/acs-setmusicvolume/computation/musicvolume_compute.h"
#include <algorithm>
#include <cmath>

float ComputeNetMusicVolume(float volume)
{
	if (!std::isfinite(volume))
		return 1.f;
	return std::min(std::max(volume, 0.f), 2.f);
}
