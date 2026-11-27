#include "flagrehome_compute.h"

#include <gtest/gtest.h>

using namespace zx::decorateuzdoom;

TEST(FlagRehome, MovesNamedBitsAndKeepsTheRest)
{
	const BitMove swap[] = { { 0x1, 0x2 }, { 0x2, 0x1 } };
	EXPECT_EQ(0x2u, ComputeMovedBits(0x1, swap, 2));
	EXPECT_EQ(0x3u, ComputeMovedBits(0x3, swap, 2));
	EXPECT_EQ(0x10u | 0x1u, ComputeMovedBits(0x12, swap, 2));
	EXPECT_EQ(0x0u, ComputeMovedBits(0x0, swap, 2));
	EXPECT_EQ(0xF0u, ComputeMovedBits(0xF0, nullptr, 0));
}

TEST(FlagRehome, ABitMovedToZeroLeavesTheWord)
{
	const BitMove drop[] = { { 0x4, 0 } };
	EXPECT_EQ(0x1u, ComputeMovedBits(0x5, drop, 1));
}

TEST(FlagRehome, Flags7SwapsTheThreeZandronumBits)
{
	const SavedFlags out = ComputeRehomedSaveFlags({ 0, 0x00020000 | 0x00000001, 0 });
	EXPECT_EQ(0x00040000u | 0x00000001u, out.flags7);
	EXPECT_EQ(0x00080000u, ComputeRehomedSaveFlags({ 0, 0x00040000, 0 }).flags7);
	EXPECT_EQ(0x00020000u, ComputeRehomedSaveFlags({ 0, 0x00080000, 0 }).flags7);
	EXPECT_EQ(0x000E0000u, ComputeRehomedSaveFlags({ 0, 0x000E0000, 0 }).flags7);
}

TEST(FlagRehome, Flags8TakesUZDoomsMbf21Bits)
{
	EXPECT_EQ(0x00020000u, ComputeRehomedSaveFlags({ 0, 0, 0x4 }).flags8);
	EXPECT_EQ(0x00010000u | 0x00800000u, ComputeRehomedSaveFlags({ 0, 0, 0x2 | 0x100 }).flags8);
	EXPECT_EQ(0x00200000u, ComputeRehomedSaveFlags({ 0, 0, 0x20 }).flags8);
	EXPECT_EQ(0x00100000u, ComputeRehomedSaveFlags({ 0, 0, 0x40 }).flags8);
	EXPECT_EQ(0xFFu << 16, ComputeRehomedSaveFlags({ 0, 0, 0x1FE }).flags8);
}

TEST(FlagRehome, FullVolDeathMovesToFlags3)
{
	const SavedFlags out = ComputeRehomedSaveFlags({ 0x8, 0, 0x1 | 0x2 });
	EXPECT_EQ(0x8u | kFlags3FullVolDeath, out.flags3);
	EXPECT_EQ(0x00010000u, out.flags8);
	EXPECT_EQ(0x8u, ComputeRehomedSaveFlags({ 0x8, 0, 0x2 }).flags3);
}

TEST(FlagRehome, LightMultAlphaLeavesRenderflagsForRenderflags2)
{
	const SavedRenderFlags out = ComputeRehomedSaveRenderFlags(0x00080000 | 0x8000 | 0x10);
	EXPECT_EQ(0x8000u | 0x10u, out.renderflags);
	EXPECT_EQ(kRenderFlags2LightMultAlpha, out.renderflags2);
	const SavedRenderFlags plain = ComputeRehomedSaveRenderFlags(0x8000);
	EXPECT_EQ(0x8000u, plain.renderflags);
	EXPECT_EQ(0u, plain.renderflags2);
}
