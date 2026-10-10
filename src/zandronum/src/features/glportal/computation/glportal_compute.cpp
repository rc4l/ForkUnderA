// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/glportal/computation/glportal_compute.h"

namespace zx { namespace glportal {

bool ComputeDrawsSkyPortalFirst(bool needsDepthBuffer, int sceneRecursion, int portalDepth)
{
	// [rc4l] A portal drawn first re-enters DrawScene before its recursion count rises, so the portal depth is what says a scene is nested.
	return !needsDepthBuffer || (sceneRecursion == 0 && portalDepth <= 1);
}

}} // namespace zx::glportal
