// [rc4l] Implementation of the MBF21 DeHackEd field conversions. See dehacked_fields_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "dehacked_fields_compute.h"

namespace zx { namespace mbf21 {

namespace { const int64_t FRACUNIT = 65536; }

int ComputeInfightingGroupStored(int dehValue)
{
	return dehValue + IG_END;
}

int ComputeProjectileGroupStored(int dehValue)
{
	return dehValue < 0 ? PG_GROUPLESS : dehValue + PG_END;
}

int ComputeSplashGroupStored(int dehValue)
{
	return dehValue + SG_END;
}

int64_t ComputeMeleeRangeFixed(int64_t dehFixedValue)
{
	return dehFixedValue - 20 * FRACUNIT;
}

bool ComputeDsdHackedEnabled(int doomVersion, int patchFormat)
{
	return patchFormat == 6 || doomVersion == 2021;
}

}} // namespace zx::mbf21
