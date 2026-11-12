// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/acsuserinfo_compute.h"

namespace zx {

bool ComputeSendsUserinfoCvar(bool unsynced, bool modCvar, bool fromACS)
{
	return !unsynced && (!fromACS || modCvar);
}

bool ComputeUserinfoMayKick(bool fullUpdateIncomplete, bool fromACS)
{
	return !fullUpdateIncomplete && !fromACS;
}

} // namespace zx
