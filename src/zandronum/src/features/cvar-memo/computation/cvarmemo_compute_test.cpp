// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the cvar memo: a name's pointer finds what was stored for it until the generation moves on.
#include "gtest/gtest.h"
#include "features/cvar-memo/computation/cvarmemo_compute.h"

#include <cstdint>

namespace
{
int g_cvarA, g_cvarB;
const char kNameA[] = "cd_effectsthrottle";
const char kNameB[] = "isusingzandronum";

// [Doruk-Aksoy] A stored pointer is found, another pointer is not, and an empty memo finds nothing.
TEST(ComputeCVarMemo, FindsWhatWasStored)
{
	ZxCVarMemo memo;
	EXPECT_EQ(ComputeCVarMemoFind(memo, kNameA, 1), nullptr);
	ComputeCVarMemoStore(memo, kNameA, &g_cvarA, 1);
	ComputeCVarMemoStore(memo, kNameB, &g_cvarB, 1);
	EXPECT_EQ(ComputeCVarMemoFind(memo, kNameA, 1), &g_cvarA);
	EXPECT_EQ(ComputeCVarMemoFind(memo, kNameB, 1), &g_cvarB);
	EXPECT_EQ(ComputeCVarMemoFind(memo, kNameA + 1, 1), nullptr);
}

// [Doruk-Aksoy] A new generation finds nothing stored under the old one, and storing under it forgets the rest.
TEST(ComputeCVarMemo, ANewGenerationForgets)
{
	ZxCVarMemo memo;
	ComputeCVarMemoStore(memo, kNameA, &g_cvarA, 1);
	ComputeCVarMemoStore(memo, kNameB, &g_cvarB, 1);
	EXPECT_EQ(ComputeCVarMemoFind(memo, kNameA, 2), nullptr);
	ComputeCVarMemoStore(memo, kNameA, &g_cvarB, 2);
	EXPECT_EQ(ComputeCVarMemoFind(memo, kNameA, 2), &g_cvarB);
	EXPECT_EQ(ComputeCVarMemoFind(memo, kNameB, 2), nullptr);
}

// [Doruk-Aksoy] Two names in one slot keep the newer, and every slot is within the memo.
TEST(ComputeCVarMemo, OneSlotKeepsTheNewer)
{
	ZxCVarMemo memo;
	const char *names = "abcdefghijklmnopqrstuvwxyz0123456789";
	for (int i = 0; i < 36; i++)
	{
		EXPECT_GE(ComputeCVarMemoSlot(names + i), 0);
		EXPECT_LT(ComputeCVarMemoSlot(names + i), kZxCVarMemoSlots);
	}
	const char *first = names, *clash = nullptr;
	for (int i = 1; i < kZxCVarMemoSlots * 64 && clash == nullptr; i++)
	{
		const char *other = reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(first) + i * 8);
		if (ComputeCVarMemoSlot(other) == ComputeCVarMemoSlot(first)) clash = other;
	}
	ASSERT_NE(clash, nullptr);
	ComputeCVarMemoStore(memo, first, &g_cvarA, 1);
	ComputeCVarMemoStore(memo, clash, &g_cvarB, 1);
	EXPECT_EQ(ComputeCVarMemoFind(memo, clash, 1), &g_cvarB);
	EXPECT_EQ(ComputeCVarMemoFind(memo, first, 1), nullptr);
}
}
