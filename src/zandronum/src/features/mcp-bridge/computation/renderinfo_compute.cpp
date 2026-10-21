// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/renderinfo_compute.h"

namespace zx { namespace mcp {

const char *ComputeRendererName(bool backendOnly, int currentrenderer)
{
	if (backendOnly) return "vulkan";
	return currentrenderer == 1 ? "opengl" : "software";
}

bool ComputeGlLive(int currentrenderer, bool backendOnly)
{
	return currentrenderer == 1 && !backendOnly;
}

}} // namespace zx::mcp
