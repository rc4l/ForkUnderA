// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/materialslots_compute.h"

using namespace zx::hwrender;

// [Doruk-Aksoy] A desktop driver's limits run to the millions, so the array takes its full kMaxMaterialSlots.
TEST(MaterialSlots, ADesktopDeviceGetsTheWholeArray)
{
	const DescriptorLimits big = { 1048576, 1048576, 4294967295u, 1048576, 1048576 };
	EXPECT_EQ((unsigned)kMaxMaterialSlots, ComputeMaterialSlotCapacity(big));
}

// [rc4l] The smallest of the five limits decides, less what the stage binds beside the array.
TEST(MaterialSlots, TheSmallestLimitDecidesLessTheStagesOtherResources)
{
	EXPECT_EQ(1024u - kOtherStageResources, ComputeMaterialSlotCapacity({ 1024, 1048576, 1048576, 1048576, 1048576 }));
	EXPECT_EQ(2048u - kOtherStageResources, ComputeMaterialSlotCapacity({ 1048576, 1048576, 1048576, 1048576, 2048 }));
	EXPECT_EQ(500u - kOtherStageResources, ComputeMaterialSlotCapacity({ 1048576, 500, 1048576, 1048576, 1048576 }));
}

// [rc4l] The Vulkan minimum (16 sampled images a stage) leaves nothing worth an array, so bindless is off and every material gets its own binding.
TEST(MaterialSlots, ASmallDeviceTurnsBindlessOff)
{
	EXPECT_EQ(0u, ComputeMaterialSlotCapacity({ 16, 16, 128, 96, 96 }));
	EXPECT_EQ(0u, ComputeMaterialSlotCapacity({ 0, 0, 0, 0, 0 }));
	EXPECT_EQ(0u, ComputeMaterialSlotCapacity({ kMinMaterialSlots + kOtherStageResources - 1, 4096, 4096, 4096, 4096 }));
	EXPECT_EQ((unsigned)kMinMaterialSlots, ComputeMaterialSlotCapacity({ kMinMaterialSlots + kOtherStageResources, 4096, 4096, 4096, 4096 }));
}

// [Doruk-Aksoy] A cap cuts every limit above it, one below leaves a limit alone, and none leaves the device's own size.
TEST(MaterialSlots, ACapDrawsAsASmallerDeviceWould)
{
	const DescriptorLimits big = { 1048576, 1048576, 4294967295u, 1048576, 1048576 };
	EXPECT_EQ((unsigned)kMaxMaterialSlots, ComputeCappedSlotCapacity(big, 0));
	EXPECT_EQ((unsigned)kMaxMaterialSlots, ComputeCappedSlotCapacity(big, -5));
	EXPECT_EQ(1024u - kOtherStageResources, ComputeCappedSlotCapacity(big, 1024));
	EXPECT_EQ(500u - kOtherStageResources, ComputeCappedSlotCapacity({ 1048576, 500, 1048576, 1048576, 1048576 }, 1024));
	EXPECT_EQ(0u, ComputeCappedSlotCapacity(big, 16));
}

TEST(MaterialSlots, DefinesGoRightAfterTheVersionLine)
{
	EXPECT_EQ("#version 450\n#define N 7\nvoid main() {}\n", ComputeShaderWithDefines("#version 450\nvoid main() {}\n", "#define N 7\n"));
	EXPECT_EQ("#version 450", ComputeShaderWithDefines("#version 450", "#define N 7\n"));
}

// [rc4l] A device with the array declares it at its size; one without gets a legal size and no FUA_BINDLESS, so the shaders leave the array out.
TEST(MaterialSlots, TheShaderDefinesFollowTheArray)
{
	EXPECT_EQ("#define FUA_MAT_SLOTS 4096\n#define FUA_BINDLESS 1\n", ComputeMaterialShaderDefines(4096));
	EXPECT_EQ("#define FUA_MAT_SLOTS 1\n", ComputeMaterialShaderDefines(0));
}

// [Doruk-Aksoy] A game translation is filed under its palette, so a redefined one gets a new key while its number stays.
TEST(MaterialSlots, AGameTranslationIsKeyedByItsPalette)
{
	EXPECT_EQ(-12, ComputeSlotTranslation(3, 12));
	EXPECT_EQ(-13, ComputeSlotTranslation(3, 13));
	EXPECT_EQ(0, ComputeSlotTranslation(0, 0));
}

// [Doruk-Aksoy] A key already negated reads back as itself, so a slot's own key normalises to the same slot.
TEST(MaterialSlots, APaletteKeyStaysItself)
{
	EXPECT_EQ(-12, ComputeSlotTranslation(-12, 12));
}

// [Doruk-Aksoy] The brightmap key is kept, though MaterialTranslation reads it as a palette like any negative one.
TEST(MaterialSlots, TheBrightmapKeyIsKept)
{
	EXPECT_EQ(kBrightmapTranslation, ComputeSlotTranslation(kBrightmapTranslation, -kBrightmapTranslation));
}

// [Doruk-Aksoy] A slot with no material is the placeholder whatever its key, the brightmap key reads as the brightmap and every other key as the material.
TEST(MaterialSlots, ASlotsKindFollowsItsMaterialAndKey)
{
	static const int material = 0;
	EXPECT_EQ(kSlotWhite, ComputeSlotKind(nullptr, 0));
	EXPECT_EQ(kSlotWhite, ComputeSlotKind(nullptr, kBrightmapTranslation));
	EXPECT_EQ(kSlotBrightmap, ComputeSlotKind(&material, kBrightmapTranslation));
	EXPECT_EQ(kSlotMaterial, ComputeSlotKind(&material, 0));
	EXPECT_EQ(kSlotMaterial, ComputeSlotKind(&material, -12));
}

// [Doruk-Aksoy] A slot's row starts after the remap entries, eight words a slot, where FUA_LAYER_ROWS_STR says, and the buffer holds every slot's.
TEST(MaterialSlots, LayerRowsFollowTheRemapEntries)
{
	EXPECT_EQ(8192u, ComputeLayerRowWord(0));
	EXPECT_EQ(8192u + 8u * 5u, ComputeLayerRowWord(5));
	EXPECT_EQ(8192u * 9u, ComputeSlotRemapWords());
}

// [Doruk-Aksoy] Two slots a word, the even layer low, the spare entry carrying its word instead of a slot, and nothing below zero.
TEST(MaterialSlots, ALayerRowPacksTwoSlotsAWord)
{
	int slots[kLayerRowEntries] = { 0 };
	slots[0] = 3; slots[1] = 4; slots[6] = 0x1234; slots[7] = 99; slots[8] = 8191; slots[15] = -2;
	unsigned row[kLayerRowWords];
	ComputeLayerRow(slots, 0xabcdu, row);
	EXPECT_EQ(3u | (4u << 16), row[0]);
	EXPECT_EQ(0u, row[1]);
	EXPECT_EQ(0x1234u | (0xabcdu << 16), row[3]);
	EXPECT_EQ(8191u, row[4]);
	EXPECT_EQ(0u, row[7]);
}
