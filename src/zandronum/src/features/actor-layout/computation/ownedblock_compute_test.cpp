// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actor-layout/computation/ownedblock_compute.h"
#include <gtest/gtest.h>

using namespace zx::actorlayout;

namespace {

struct Block
{
	int a = 7;
	int b = 0;
};

} // namespace

// [Doruk-Aksoy] No block reads the defaults, and every such read is the same shared set.
TEST(OwnedBlock, NoBlockReadsTheDefaults)
{
	const Block *none = nullptr;
	EXPECT_EQ(ComputeBlockRead(none).a, 7);
	EXPECT_EQ(&ComputeBlockRead(none), &ComputeBlockRead(none));
}

// [Doruk-Aksoy] The first write makes the block from the defaults, and later reads and writes use it.
TEST(OwnedBlock, TheFirstWriteMakesTheBlock)
{
	Block *block = nullptr;
	ComputeBlockWrite(block).b = 3;
	ASSERT_NE(block, nullptr);
	EXPECT_EQ(block->a, 7);
	Block *const made = block;
	ComputeBlockWrite(block).a = 1;
	EXPECT_EQ(block, made);
	EXPECT_EQ(ComputeBlockRead<Block>(block).a, 1);
	EXPECT_EQ(ComputeBlockRead<Block>(block).b, 3);
	delete block;
}

// [Doruk-Aksoy] A copy has its own block holding the other's values, and none when the other had none.
TEST(OwnedBlock, ACopyOwnsItsOwnBlock)
{
	Block other;
	other.b = 5;
	Block *const copy = ComputeBlockCopy(&other);
	ASSERT_NE(copy, nullptr);
	EXPECT_NE(copy, &other);
	EXPECT_EQ(copy->b, 5);
	delete copy;
	EXPECT_EQ(ComputeBlockCopy<Block>(nullptr), nullptr);
}

// [Doruk-Aksoy] Assigning takes the other's values into this block, making it when needed, and frees it when the other has none.
TEST(OwnedBlock, AssignFollowsTheOther)
{
	Block other;
	other.b = 9;
	Block *mine = nullptr;
	ComputeBlockAssign(mine, &other);
	ASSERT_NE(mine, nullptr);
	EXPECT_EQ(mine->b, 9);
	other.b = 4;
	Block *const kept = mine;
	ComputeBlockAssign(mine, &other);
	EXPECT_EQ(mine, kept);
	EXPECT_EQ(mine->b, 4);
	ComputeBlockAssign<Block>(mine, nullptr);
	EXPECT_EQ(mine, nullptr);
}
