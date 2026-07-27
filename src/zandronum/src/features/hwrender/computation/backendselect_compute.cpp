// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/hwrender/computation/backendselect_compute.h"

namespace zx
{

RenderBackend ResolveBackend(int vidHwrender, bool vulkanSupported)
{
	switch (vidHwrender)
	{
	case static_cast<int>(RenderBackend::CoreGL):
		return RenderBackend::CoreGL;
	case static_cast<int>(RenderBackend::Vulkan):
		// [rc4l] Vulkan requested: honor it only if the build/platform supports it, else fall back
		// to the core GL path, which is still modern -- never silently back to legacy.
		return vulkanSupported ? RenderBackend::Vulkan : RenderBackend::CoreGL;
	default:
		// [rc4l] 0 and any out-of-range value: the safe default the engine has always shipped.
		return RenderBackend::LegacyGL;
	}
}

bool NeedsModernContext(RenderBackend backend)
{
	return backend != RenderBackend::LegacyGL;
}

bool NeedsRestart(RenderBackend current, RenderBackend requested)
{
	return current != requested;
}

} // namespace zx
