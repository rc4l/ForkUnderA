// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What `dumprenderer` may touch, decided from the launch's renderer state alone so the glue in mcp_renderinfo.cpp never reaches GL where there is none.

#ifndef ZX_RENDERINFO_COMPUTE_H
#define ZX_RENDERINFO_COMPUTE_H

namespace zx { namespace mcp {

// [rc4l] The renderer's name as dumprenderer reports it: the backend wins over currentrenderer, which a Vulkan-alone launch leaves at 1.
const char *ComputeRendererName(bool backendOnly, int currentrenderer);

// [rc4l] Whether a GL context exists to read from: currentrenderer says GL, and the launch did not go backend-only without one.
bool ComputeGlLive(int currentrenderer, bool backendOnly);

}} // namespace zx::mcp

#endif // ZX_RENDERINFO_COMPUTE_H
