// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/backendchoice_compute.h"

namespace zx { namespace hwrender {

bool ComputeWantsBackendOnly(int vidBackend, bool argVulkan, bool argGL, bool built)
{
	if (!built || argGL) return false;
	return argVulkan || vidBackend != kVidBackendGL;
}

}} // namespace zx::hwrender
