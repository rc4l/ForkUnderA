// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-protocol/computation/commandcapture_compute.h"

#include <gtest/gtest.h>

using namespace zx::netprotocol;

TEST(ComputeCommandIds, OrdinaryExtendedAndEmpty)
{
	int id, ext;
	const unsigned char plain[] = { 40, 1, 2 };
	ComputeCommandIds(plain, 3, 200, id, ext);
	EXPECT_EQ(40, id);
	EXPECT_EQ(-1, ext);
	const unsigned char extended[] = { 200, 17, 9 };
	ComputeCommandIds(extended, 3, 200, id, ext);
	EXPECT_EQ(200, id);
	EXPECT_EQ(17, ext);
	const unsigned char lone[] = { 200 };
	ComputeCommandIds(lone, 1, 200, id, ext);
	EXPECT_EQ(200, id);
	EXPECT_EQ(-1, ext);
	ComputeCommandIds(nullptr, 0, 200, id, ext);
	EXPECT_EQ(-1, id);
	EXPECT_EQ(-1, ext);
}

TEST(ComputeCaptureLine, TicClientReliabilityIdsSizeAndHex)
{
	const unsigned char bytes[] = { 200, 17, 0xab, 0x05 };
	EXPECT_EQ("350 2 r 200 17 4 c811ab05", ComputeCaptureLine(350, 2, true, bytes, 4, 200));
	EXPECT_EQ("7 0 u 200 17 4 c811ab05", ComputeCaptureLine(7, 0, false, bytes, 4, 200));
	EXPECT_EQ("1 1 r -1 -1 0 ", ComputeCaptureLine(1, 1, true, nullptr, 0, 200));
}
