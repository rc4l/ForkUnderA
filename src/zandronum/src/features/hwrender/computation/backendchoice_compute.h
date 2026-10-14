// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which renderer a launch draws with: the Vulkan backend unless the player chose GL or the backend cannot start.

#ifndef ZX_BACKENDCHOICE_COMPUTE_H
#define ZX_BACKENDCHOICE_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] vid_backend's values: GL (Legacy) and the Vulkan backend (Recommended), the default.
enum { kVidBackendGL = 0, kVidBackendVulkan = 1 };

// [rc4l] Whether a launch asks for the backend alone: -gl and -vulkan override vid_backend, -gl winning, and a build without the backend never does.
bool ComputeWantsBackendOnly(int vidBackend, bool argVulkan, bool argGL, bool built);

}} // namespace zx::hwrender

#endif // ZX_BACKENDCHOICE_COMPUTE_H
