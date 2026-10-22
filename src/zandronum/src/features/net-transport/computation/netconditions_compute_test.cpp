// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-transport/computation/netconditions_compute.h"

#include <gtest/gtest.h>

using namespace zx::nettransport;

TEST(ComputeFakeDelayMs, HalfOfThePingEachWayTheOddMillisecondOut)
{
	EXPECT_EQ(50, ComputeFakeDelayMs(100, true));
	EXPECT_EQ(50, ComputeFakeDelayMs(100, false));
	EXPECT_EQ(51, ComputeFakeDelayMs(101, true));
	EXPECT_EQ(50, ComputeFakeDelayMs(101, false));
	EXPECT_EQ(0, ComputeFakeDelayMs(0, true));
	EXPECT_EQ(0, ComputeFakeDelayMs(-20, false));
}

TEST(ComputeFakeDrop, NoLossDropsNothingAndLeavesTheGeneratorAlone)
{
	unsigned state = 7;
	for (int i = 0; i < 100; i++) EXPECT_FALSE(ComputeFakeDrop(state, 0.f));
	EXPECT_EQ(7u, state);
}

TEST(ComputeFakeDrop, AllLossDropsEverything)
{
	unsigned state = 7;
	for (int i = 0; i < 100; i++) EXPECT_TRUE(ComputeFakeDrop(state, 100.f));
}

TEST(ComputeFakeDrop, ARateDropsAboutThatShareTheSameWayEachRun)
{
	unsigned a = 12345, b = 12345;
	int dropped = 0;
	for (int i = 0; i < 10000; i++)
	{
		const bool da = ComputeFakeDrop(a, 10.f);
		EXPECT_EQ(da, ComputeFakeDrop(b, 10.f));
		if (da) dropped++;
	}
	EXPECT_NEAR(1000, dropped, 150);
}

TEST(ComputeHoldPacket, PacketsGoInTheOrderHeldOnceDue)
{
	HeldPackets held;
	const unsigned char one[] = { 1, 2 }, two[] = { 3 }, addr[] = { 9, 9, 9 };
	ComputeHoldPacket(held, 1000, 50, one, 2, addr, 3);
	ComputeHoldPacket(held, 1010, 50, two, 1, addr, 3);
	HeldPacket out;
	EXPECT_FALSE(ComputeTakeDuePacket(held, 1049, out));
	ASSERT_TRUE(ComputeTakeDuePacket(held, 1050, out));
	EXPECT_EQ(std::vector<unsigned char>({ 1, 2 }), out.bytes);
	EXPECT_EQ(std::vector<unsigned char>({ 9, 9, 9 }), out.address);
	EXPECT_EQ(3, out.addressLength);
	EXPECT_FALSE(ComputeTakeDuePacket(held, 1059, out));
	ASSERT_TRUE(ComputeTakeDuePacket(held, 2000, out));
	EXPECT_EQ(std::vector<unsigned char>({ 3 }), out.bytes);
	EXPECT_FALSE(ComputeTakeDuePacket(held, 3000, out));
}

TEST(ComputeHoldPacket, ANegativeDelayGoesAtOnceAndEmptyPartsStayEmpty)
{
	HeldPackets held;
	ComputeHoldPacket(held, 500, -10, nullptr, 0, nullptr, 0);
	HeldPacket out;
	ASSERT_TRUE(ComputeTakeDuePacket(held, 500, out));
	EXPECT_TRUE(out.bytes.empty());
	EXPECT_TRUE(out.address.empty());
	EXPECT_EQ(0, out.addressLength);
}
