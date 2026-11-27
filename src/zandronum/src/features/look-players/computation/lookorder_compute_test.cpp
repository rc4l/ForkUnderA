// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "lookorder_compute.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <vector>

namespace
{

// [Doruk-Aksoy] The draws a test scripts, the ranges each draw was asked for, and how many it answered.
std::vector<int> g_script;
std::vector<int> g_ranges;
size_t g_next = 0;

int Scripted(int range)
{
	g_ranges.push_back(range);
	return g_script[g_next++];
}

void Script(std::vector<int> draws)
{
	g_script = draws;
	g_ranges.clear();
	g_next = 0;
}

std::vector<int> Order(const bool *inGame, int slots)
{
	ZxLookOrder order;
	ComputeLookOrderStart(order, inGame, slots);
	std::vector<int> picked;
	for (int player = ComputeLookOrderNext(order, Scripted); player >= 0; player = ComputeLookOrderNext(order, Scripted))
		picked.push_back(player);
	return picked;
}

} // namespace

TEST(LookOrder, NoPlayerInTheGameEndsAtOnceWithoutADraw)
{
	bool inGame[4] = { false, false, false, false };
	Script({});
	EXPECT_TRUE(Order(inGame, 4).empty());
	EXPECT_TRUE(g_ranges.empty());
}

TEST(LookOrder, ALonePlayerIsTriedWithoutADraw)
{
	bool inGame[4] = { false, false, true, false };
	Script({});
	EXPECT_EQ(Order(inGame, 4), std::vector<int>({ 2 }));
	EXPECT_TRUE(g_ranges.empty());
}

TEST(LookOrder, EachDrawIsOverThePlayersLeftAndTheLastNeedsNone)
{
	bool inGame[6] = { true, false, true, true, false, true };
	Script({ 0, 2, 0 });
	// [Doruk-Aksoy] Left 0 2 3 5: draw 0 takes 0 (5 moves in), left 5 2 3: draw 2 takes 3, left 5 2: draw 0 takes 5, then 2.
	EXPECT_EQ(Order(inGame, 6), std::vector<int>({ 0, 3, 5, 2 }));
	EXPECT_EQ(g_ranges, std::vector<int>({ 4, 3, 2 }));
}

TEST(LookOrder, EveryDrawSequenceGivesADifferentOrderSoEachOrderIsEquallyLikely)
{
	bool inGame[5] = { false, true, true, false, true };
	std::set<std::vector<int>> orders;
	for (int first = 0; first < 3; ++first)
	{
		for (int second = 0; second < 2; ++second)
		{
			Script({ first, second });
			orders.insert(Order(inGame, 5));
		}
	}
	EXPECT_EQ(orders.size(), 6u);
}

TEST(LookOrder, SlotsPastTheLimitAreLeftOut)
{
	bool inGame[kZxLookSlots + 1];
	for (bool &slot : inGame)
		slot = false;
	inGame[kZxLookSlots - 1] = true;
	inGame[kZxLookSlots] = true;
	Script({});
	EXPECT_EQ(Order(inGame, kZxLookSlots + 1), std::vector<int>({ kZxLookSlots - 1 }));
}
