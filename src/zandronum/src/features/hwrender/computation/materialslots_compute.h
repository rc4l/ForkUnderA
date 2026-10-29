// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How many textures the bindless material array holds on the device in hand, and the shader source that declares it.
// [Doruk-Aksoy] And the translation key each slot is filed under.

#ifndef ZX_MATERIALSLOTS_COMPUTE_H
#define ZX_MATERIALSLOTS_COMPUTE_H

#include <string>

namespace zx { namespace hwrender {

// [rc4l] The most the array is ever given, past what content plausibly asks for (a nine-PWAD Whodunit load wants 835), the fewest worth keeping bindless for, and what the rest of a fragment stage may bind beside it.
enum { kMaxMaterialSlots = 4096, kMinMaterialSlots = 64, kOtherStageResources = 32 };

// [rc4l] The translation a brightmap's slot is filed under: no real one is this, so a material and its brightmap never share a slot.
static const int kBrightmapTranslation = -0x7fffffff;

// [Doruk-Aksoy] The key a slot or binding files `translation` under: a brightmap's as given, any other as its palette's index negated (`contentIndex`, MaterialTranslation's), since a game translation keeps its number while its colours change.
int ComputeSlotTranslation(int translation, int contentIndex);

// [Doruk-Aksoy] What a slot holds: the white placeholder (no material), a material in its translation, or a material's brightmap.
enum SlotKind { kSlotWhite, kSlotMaterial, kSlotBrightmap };

// [Doruk-Aksoy] The kind of slot a material and translation key make.
SlotKind ComputeSlotKind(const void *material, int translation);

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

// [rc4l] The defines every backend shader is compiled with: the array's size, at least one so a declaration stays legal, and FUA_BINDLESS where the device has the array at all.
std::string ComputeMaterialShaderDefines(unsigned slots);

// [rc4l] Where a material array's binding from slot `from` stops: every slot to its capacity the first time (from 0, the placeholder past the filled ones), else only the slots filled since.
unsigned ComputeMaterialBindEnd(unsigned from, unsigned capacity, unsigned filled);

}} // namespace zx::hwrender

#endif // ZX_MATERIALSLOTS_COMPUTE_H
