// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the name memo: a name's pointer finds what was stored for it until the generation moves on.
#include "gtest/gtest.h"
#include "features/name-memo/computation/namememo_compute.h"

#include <cstdint>
#include <memory>

namespace
{
int g_cvarA, g_cvarB;
const char kNameA[] = "cd_effectsthrottle";
const char kNameB[] = "isusingzandronum";

// [Doruk-Aksoy] A thread's memo is made empty on its first use and the same one comes back after.
TEST(ComputeNameMemo, MadeOnFirstUseThenKept)
{
	std::unique_ptr<ZxNameMemo> holder;
	ZxNameMemo &first = ComputeNameMemoFor(holder);
	ASSERT_TRUE(holder != nullptr);
	EXPECT_EQ(first.generation, 0u);
	for (int i = 0; i < kZxNameMemoSlots; i++)
	{
		EXPECT_EQ(first.key[i], nullptr);
		EXPECT_EQ(first.value[i], nullptr);
	}
	ComputeNameMemoStore(first, kNameA, &g_cvarA, 1);
	ZxNameMemo &again = ComputeNameMemoFor(holder);
	EXPECT_EQ(&again, &first);
	EXPECT_EQ(ComputeNameMemoFind(again, kNameA, 1), &g_cvarA);
}

// [Doruk-Aksoy] A stored pointer is found, another pointer is not, and an empty memo finds nothing.
TEST(ComputeNameMemo, FindsWhatWasStored)
{
	ZxNameMemo memo;
	EXPECT_EQ(ComputeNameMemoFind(memo, kNameA, 1), nullptr);
	ComputeNameMemoStore(memo, kNameA, &g_cvarA, 1);
	ComputeNameMemoStore(memo, kNameB, &g_cvarB, 1);
	EXPECT_EQ(ComputeNameMemoFind(memo, kNameA, 1), &g_cvarA);
	EXPECT_EQ(ComputeNameMemoFind(memo, kNameB, 1), &g_cvarB);
	EXPECT_EQ(ComputeNameMemoFind(memo, kNameA + 1, 1), nullptr);
}

// [Doruk-Aksoy] A new generation finds nothing stored under the old one, and storing under it forgets the rest.
TEST(ComputeNameMemo, ANewGenerationForgets)
{
	ZxNameMemo memo;
	ComputeNameMemoStore(memo, kNameA, &g_cvarA, 1);
	ComputeNameMemoStore(memo, kNameB, &g_cvarB, 1);
	EXPECT_EQ(ComputeNameMemoFind(memo, kNameA, 2), nullptr);
	ComputeNameMemoStore(memo, kNameA, &g_cvarB, 2);
	EXPECT_EQ(ComputeNameMemoFind(memo, kNameA, 2), &g_cvarB);
	EXPECT_EQ(ComputeNameMemoFind(memo, kNameB, 2), nullptr);
}

// [Doruk-Aksoy] Two names in one slot keep the newer, and every slot is within the memo.
TEST(ComputeNameMemo, OneSlotKeepsTheNewer)
{
	ZxNameMemo memo;
	const char *names = "abcdefghijklmnopqrstuvwxyz0123456789";
	for (int i = 0; i < 36; i++)
	{
		EXPECT_GE(ComputeNameMemoSlot(names + i), 0);
		EXPECT_LT(ComputeNameMemoSlot(names + i), kZxNameMemoSlots);
	}
	const char *first = names, *clash = nullptr;
	for (int i = 1; i < kZxNameMemoSlots * 64 && clash == nullptr; i++)
	{
		const char *other = reinterpret_cast<const char *>(reinterpret_cast<uintptr_t>(first) + i * 8);
		if (ComputeNameMemoSlot(other) == ComputeNameMemoSlot(first)) clash = other;
	}
	ASSERT_NE(clash, nullptr);
	ComputeNameMemoStore(memo, first, &g_cvarA, 1);
	ComputeNameMemoStore(memo, clash, &g_cvarB, 1);
	EXPECT_EQ(ComputeNameMemoFind(memo, clash, 1), &g_cvarB);
	EXPECT_EQ(ComputeNameMemoFind(memo, first, 1), nullptr);
}
}
