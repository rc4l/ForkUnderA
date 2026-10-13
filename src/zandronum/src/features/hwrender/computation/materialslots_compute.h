// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How many textures the bindless material array holds on the device in hand, and the shader source that declares it.

#ifndef ZX_MATERIALSLOTS_COMPUTE_H
#define ZX_MATERIALSLOTS_COMPUTE_H

#include <string>

namespace zx { namespace hwrender {

// [rc4l] The most the array is ever given, past what content plausibly asks for (a nine-PWAD Whodunit load wants 835), the fewest worth keeping bindless for, and what the rest of a fragment stage may bind beside it.
enum { kMaxMaterialSlots = 4096, kMinMaterialSlots = 64, kOtherStageResources = 32 };

// [rc4l] The Vulkan limits a fragment stage's sampler array counts against, as VkPhysicalDeviceLimits names them.
struct DescriptorLimits
{
	unsigned perStageSampledImages, perStageSamplers, perStageResources;
	unsigned setSampledImages, setSamplers;
};

// [rc4l] The array's size: the smallest limit less what the stage binds beside it, at most kMaxMaterialSlots, and 0 (bindless off, a binding per material instead) when that leaves fewer than kMinMaterialSlots.
unsigned ComputeMaterialSlotCapacity(const DescriptorLimits &limits);

// [rc4l] GLSL `source` with `defines` inserted after its first line, the #version a shader must start with; a source with no line break is returned as it was.
std::string ComputeShaderWithDefines(const char *source, const std::string &defines);

}} // namespace zx::hwrender

#endif // ZX_MATERIALSLOTS_COMPUTE_H
