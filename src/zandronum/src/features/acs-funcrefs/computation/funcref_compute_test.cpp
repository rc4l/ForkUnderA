// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "features/acs-funcrefs/computation/funcref_compute.h"

using namespace zx::acsfuncrefs;

// [Doruk-Aksoy] Mods keep these values in variables and savegames, so the layout never moves.
TEST(AcsFuncRefs, TheBitsNeverMove)
{
	EXPECT_EQ(uint32_t(1) << 19, kStableBit);
	EXPECT_EQ(20, kLibraryShift);
	EXPECT_EQ(0xFFFF, kIndexMask);
	EXPECT_EQ(4095, kMaxLibrary);
}

TEST(AcsFuncRefs, AStableRefNamesItsLibraryAndFunction)
{
	const int32_t ref = ComputeFuncRefStable(5, 3);
	EXPECT_EQ(int32_t((3u << 20) | (1u << 19) | 5u), ref);
	const FuncRef decoded = ComputeFuncRefDecode(ref);
	EXPECT_TRUE(decoded.stable);
	EXPECT_EQ(3, decoded.library);
	EXPECT_EQ(5, decoded.index);
}

// [Doruk-Aksoy] 0 is a null function reference, so the first function of the first library must not encode to it.
TEST(AcsFuncRefs, AStableRefIsNeverNull)
{
	EXPECT_NE(0, ComputeFuncRefStable(0, 0));
	EXPECT_TRUE(ComputeFuncRefDecode(ComputeFuncRefStable(0, 0)).stable);
}

TEST(AcsFuncRefs, LibrariesPastTheSignBitDecode)
{
	const int32_t ref = ComputeFuncRefStable(0xFFFF, kMaxLibrary);
	EXPECT_LT(ref, 0);
	const FuncRef decoded = ComputeFuncRefDecode(ref);
	EXPECT_TRUE(decoded.stable);
	EXPECT_EQ(kMaxLibrary, decoded.library);
	EXPECT_EQ(0xFFFF, decoded.index);
}

TEST(AcsFuncRefs, WhatDoesNotFitHasNoStableForm)
{
	EXPECT_EQ(0, ComputeFuncRefStable(0x10000, 1));
	EXPECT_EQ(0, ComputeFuncRefStable(-1, 1));
	EXPECT_EQ(0, ComputeFuncRefStable(1, -1));
	EXPECT_EQ(0, ComputeFuncRefStable(1, kMaxLibrary + 1));
}

// [Doruk-Aksoy] A reference made by an older build (a load position and a byte-sized index) reads exactly as PCD_CALLSTACK always read it.
TEST(AcsFuncRefs, OldRefsDecodeAsBefore)
{
	const FuncRef decoded = ComputeFuncRefDecode(int32_t((3u << 20) | 7u));
	EXPECT_FALSE(decoded.stable);
	EXPECT_EQ(3, decoded.library);
	EXPECT_EQ(7, decoded.index);
	const FuncRef high = ComputeFuncRefDecode(int32_t((2049u << 20) | 0x40007u));
	EXPECT_FALSE(high.stable);
	EXPECT_EQ(2049, high.library);
	EXPECT_EQ(7, high.index);
}

TEST(AcsFuncRefs, ALibraryIsNumberedByItsPlaceAmongLibraryLumps)
{
	// [Doruk-Aksoy] Lumps 1, 2 and 5 are libraries.
	const std::vector<bool> library = { false, true, true, false, false, true };
	const auto isLibrary = [&](int lump) { return bool(library[lump]); };
	EXPECT_EQ(0, ComputeLibraryOrdinal(1, isLibrary));
	EXPECT_EQ(1, ComputeLibraryOrdinal(2, isLibrary));
	EXPECT_EQ(2, ComputeLibraryOrdinal(5, isLibrary));
	EXPECT_EQ(-1, ComputeLibraryOrdinal(3, isLibrary));
	EXPECT_EQ(-1, ComputeLibraryOrdinal(-1, isLibrary));
}

TEST(AcsFuncRefs, ALibraryNumberLeadsBackToItsLump)
{
	const std::vector<bool> library = { false, true, true, false, false, true };
	const auto isLibrary = [&](int lump) { return bool(library[lump]); };
	const int count = int(library.size());
	for (int lump : { 1, 2, 5 })
		EXPECT_EQ(lump, ComputeLibraryLump(ComputeLibraryOrdinal(lump, isLibrary), count, isLibrary));
	EXPECT_EQ(-1, ComputeLibraryLump(3, count, isLibrary));
	EXPECT_EQ(-1, ComputeLibraryLump(-1, count, isLibrary));
}

TEST(AcsFuncRefs, ALoadedLibraryIsFoundByItsNumber)
{
	// [Doruk-Aksoy] A map's own BEHAVIOR (-1) loads first, then libraries 4 and 2.
	const std::vector<int> ordinals = { -1, 4, 2 };
	const auto ordinalOf = [&](int module) { return ordinals[module]; };
	EXPECT_EQ(1, ComputeModuleOfLibrary(3, 4, ordinalOf));
	EXPECT_EQ(2, ComputeModuleOfLibrary(3, 2, ordinalOf));
	EXPECT_EQ(-1, ComputeModuleOfLibrary(3, 7, ordinalOf));
	EXPECT_EQ(-1, ComputeModuleOfLibrary(3, -1, ordinalOf));
}
