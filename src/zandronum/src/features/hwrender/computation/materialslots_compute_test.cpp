// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/materialslots_compute.h"

using namespace zx::hwrender;

// [rc4l] A desktop driver's limits run to the millions, so the array takes its full 4096.
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

// [rc4l] A sprite's slot made after the traced array was filled is bound alone, never past the array.
TEST(MaterialSlots, AFirstBindCoversTheArrayAndALaterOneOnlyTheNewSlots)
{
	EXPECT_EQ(4096u, ComputeMaterialBindEnd(0, 4096, 5));
	EXPECT_EQ(9u, ComputeMaterialBindEnd(5, 4096, 9));
	EXPECT_EQ(4096u, ComputeMaterialBindEnd(4000, 4096, 5000));
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
