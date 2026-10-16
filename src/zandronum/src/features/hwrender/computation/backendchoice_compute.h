// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which renderer a launch draws with: the Vulkan backend unless the player chose GL or the backend cannot start.

#ifndef ZX_BACKENDCHOICE_COMPUTE_H
#define ZX_BACKENDCHOICE_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] vid_backend's values: GL (Legacy), the Vulkan backend (Recommended), the default, and the same backend on WebGPU.
enum { kVidBackendGL = 0, kVidBackendVulkan = 1, kVidBackendWebGPU = 2 };

// [rc4l] The graphics API the backend's device is made on.
enum BackendApi { kBackendApiVulkan = 0, kBackendApiWebGPU = 1 };

// [rc4l] Whether a launch asks for the backend alone: -gl and -vulkan override vid_backend, -gl winning, and a build without the backend never does.
bool ComputeWantsBackendOnly(int vidBackend, bool argVulkan, bool argGL, bool built);

// [rc4l] WebGPU where the build has nothing else, or has both and -webgpu or the setting asks for it without -vulkan; Vulkan otherwise.
BackendApi ComputeBackendApi(int vidBackend, bool argVulkan, bool argWebGPU, bool vulkanBuilt, bool webgpuBuilt);

// [rc4l] vid_backend as this build can honour it: WebGPU in a build whose only renderer it is (a browser's), Vulkan for WebGPU where WebGPU is not built, else as asked.
int ComputeVidBackendFor(int wanted, bool vulkanBuilt, bool webgpuBuilt);

// [rc4l] Whether the player has a renderer to choose, which a build whose only renderer is WebGPU does not.
bool ComputeVidBackendChoosable(bool vulkanBuilt, bool webgpuBuilt);

}} // namespace zx::hwrender

#endif // ZX_BACKENDCHOICE_COMPUTE_H
