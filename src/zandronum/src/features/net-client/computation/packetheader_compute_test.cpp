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
