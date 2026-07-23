// [rc4l] The renderer-backend selection the video menu offers and the video init obeys.
//
// `vid_hwrender` picks the backend: 0 legacy GL (compatibility context, the current renderer), 1
// core GL (the ported backend), 2 Vulkan. Because a GL context is core-or-compat for its whole life
// and Vulkan is a different device entirely, changing the backend needs a restart -- the menu shows
// the choice, this decides what it means. Kept pure so the clamp/fallback/restart rules are tested
// once and shared by the menu label and the init path, rather than re-derived (and diverging) in each.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_BACKENDSELECT_COMPUTE_H
#define ZX_BACKENDSELECT_COMPUTE_H

namespace zx
{

enum class RenderBackend
{
	LegacyGL = 0, // fixed-function + GLSL 1.20 on a compatibility context (today's renderer)
	CoreGL = 1,   // ported UZDoom GL backend on a core context
	Vulkan = 2,   // ported UZDoom Vulkan backend
};

// [rc4l] Map a raw vid_hwrender value to a backend, clamping out-of-range values to LegacyGL and
// falling back when a backend is unavailable on this build/platform. Vulkan without support falls
// back to CoreGL (still the modern path); an out-of-range int is treated as LegacyGL (the safe
// default the engine has always shipped).
RenderBackend ResolveBackend(int vidHwrender, bool vulkanSupported);

// [rc4l] Does this backend need a Core / non-fixed-function GL context (or none, for Vulkan)?
// LegacyGL is the only one that wants a compatibility context.
bool NeedsModernContext(RenderBackend backend);

// [rc4l] Switching between two resolved backends always crosses a context-type or device boundary,
// so it cannot happen live -- true means "requires an engine restart to take effect".
bool NeedsRestart(RenderBackend current, RenderBackend requested);

} // namespace zx

#endif // ZX_BACKENDSELECT_COMPUTE_H
