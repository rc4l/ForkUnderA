// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/computation/packetheader_compute.h"

#include <gtest/gtest.h>

using namespace zx::netclient;

TEST(ComputePacketHeaderKind, UnreliableReliableAndStray)
{
	const int header = 3, unreliable = 4;
	EXPECT_EQ(kPacketUnreliable, ComputePacketHeaderKind(unreliable, header, unreliable));
	EXPECT_EQ(kPacketReliable, ComputePacketHeaderKind(header, header, unreliable));
	// [rc4l] A connection-phase command arriving late, and an empty packet's -1, carry no sequence.
	EXPECT_EQ(kPacketStray, ComputePacketHeaderKind(0, header, unreliable));
	EXPECT_EQ(kPacketStray, ComputePacketHeaderKind(-1, header, unreliable));
	EXPECT_EQ(kPacketStray, ComputePacketHeaderKind(200, header, unreliable));
}

TEST(ComputeNextPacketSlot, EverySlotIsUsedBeforeOneIsReused)
{
	// [rc4l] The client's index was a byte, so a 2048-slot ring kept only 256 packets.
	const unsigned slots = 2048;
	unsigned slot = 0, used = 0;
	do
	{
		slot = ComputeNextPacketSlot(slot, slots);
		used++;
	} while (slot != 0);
	EXPECT_EQ(slots, used);
	EXPECT_EQ(300u, ComputeNextPacketSlot(299, slots));
	EXPECT_EQ(0u, ComputeNextPacketSlot(slots - 1, slots));
}

TEST(ComputeOverwritesSlot, OnlyBytesThatMeetCount)
{
	EXPECT_TRUE(ComputeOverwritesSlot(100, 50, 120, 10));
	EXPECT_TRUE(ComputeOverwritesSlot(100, 50, 90, 20));
	EXPECT_TRUE(ComputeOverwritesSlot(100, 50, 149, 1));
	EXPECT_TRUE(ComputeOverwritesSlot(100, 50, 0, 1000));
	// [rc4l] Neighbours that touch end to start share no byte.
	EXPECT_FALSE(ComputeOverwritesSlot(100, 50, 150, 10));
	EXPECT_FALSE(ComputeOverwritesSlot(100, 50, 90, 10));
	// [rc4l] An empty slot or an empty write holds or writes nothing.
	EXPECT_FALSE(ComputeOverwritesSlot(100, 0, 100, 10));
	EXPECT_FALSE(ComputeOverwritesSlot(100, 50, 120, 0));
}
