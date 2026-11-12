// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/materialslots_compute.h"

#include <initializer_list>
#include <string.h>

namespace zx { namespace hwrender {

unsigned ComputeLayerRowWord(int slot)
{
	return kMaxMaterialSlots + (unsigned)slot * kLayerRowWords;
}

unsigned ComputeSlotRemapWords()
{
	return ComputeLayerRowWord(kMaxMaterialSlots);
}

void ComputeLayerRow(const int (&slots)[kLayerRowEntries], unsigned spare, unsigned (&row)[kLayerRowWords])
{
	for (int w = 0; w < kLayerRowWords; w++)
	{
		unsigned half[2];
		for (int h = 0; h < 2; h++)
		{
			const int entry = w * 2 + h;
			half[h] = (entry == kLayerSpareEntry ? spare : (unsigned)(slots[entry] > 0 ? slots[entry] : 0)) & 0xffffu;
		}
		row[w] = half[0] | (half[1] << 16);
	}
}

unsigned ComputeMaterialSlotCapacity(const DescriptorLimits &limits)
{
	const unsigned counted[] = { limits.perStageSampledImages, limits.perStageSamplers, limits.perStageResources,
		limits.setSampledImages, limits.setSamplers };
	unsigned least = (unsigned)kMaxMaterialSlots + kOtherStageResources;
	for (unsigned c : counted) if (c < least) least = c;
	const unsigned room = least > (unsigned)kOtherStageResources ? least - kOtherStageResources : 0;
	if (room < (unsigned)kMinMaterialSlots) return 0;
	return room < (unsigned)kMaxMaterialSlots ? room : (unsigned)kMaxMaterialSlots;
}

unsigned ComputeCappedSlotCapacity(const DescriptorLimits &limits, int cap)
{
	DescriptorLimits l = limits;
	if (cap > 0)
		for (unsigned *v : { &l.perStageSampledImages, &l.perStageSamplers, &l.perStageResources, &l.setSampledImages, &l.setSamplers })
			if (*v > (unsigned)cap) *v = (unsigned)cap;
	return ComputeMaterialSlotCapacity(l);
}

std::string ComputeShaderWithDefines(const char *source, const std::string &defines)
{
	const char *eol = strchr(source, '\n');
	if (eol == NULL) return source;
	std::string out(source, eol + 1);
	out += defines;
	out += eol + 1;
	return out;
}

std::string ComputeMaterialShaderDefines(unsigned slots)
{
	std::string d = "#define FUA_MAT_SLOTS " + std::to_string(slots > 0 ? slots : 1u) + "\n";
	if (slots > 0) d += "#define FUA_BINDLESS 1\n";
	return d;
}

int ComputeSlotTranslation(int translation, int contentIndex)
{
	return translation == kBrightmapTranslation ? translation : -contentIndex;
}

SlotKind ComputeSlotKind(const void *material, int translation)
{
	if (material == nullptr) return kSlotWhite;
	return translation == kBrightmapTranslation ? kSlotBrightmap : kSlotMaterial;
}

}} // namespace zx::hwrender
