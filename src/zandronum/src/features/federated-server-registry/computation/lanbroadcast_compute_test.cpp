// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/federated-server-registry/computation/lanbroadcast_compute.h"

using namespace zx;

namespace
{

// Reads as "a host at ip broadcasts to ..." -- returns the four octets packed for easy comparison.
struct Bcast { unsigned char b[4]; };

Bcast Of(unsigned char a, unsigned char b, unsigned char c, unsigned char d)
{
	const unsigned char ip[4] = { a, b, c, d };
	Bcast out;
	ComputeSubnetBroadcast(ip, out.b);
	return out;
}

::testing::AssertionResult Is(const Bcast &got, int a, int b, int c, int d)
{
	if (got.b[0] == a && got.b[1] == b && got.b[2] == c && got.b[3] == d)
		return ::testing::AssertionSuccess();
	return ::testing::AssertionFailure()
		<< "got " << int(got.b[0]) << "." << int(got.b[1]) << "." << int(got.b[2]) << "." << int(got.b[3])
		<< ", expected " << a << "." << b << "." << c << "." << d;
}

} // namespace

TEST(LanBroadcast, ClassCHomeNetwork)
{
	// The overwhelmingly common home LAN.
	EXPECT_TRUE(Is(Of(192, 168, 1, 42), 192, 168, 1, 255));
}

TEST(LanBroadcast, ClassBTenOnAPrivateRange)
{
	EXPECT_TRUE(Is(Of(172, 16, 5, 9), 172, 16, 255, 255));
}

TEST(LanBroadcast, ClassATenNetwork)
{
	EXPECT_TRUE(Is(Of(10, 3, 4, 5), 10, 255, 255, 255));
}

TEST(LanBroadcast, ClassABoundaryOneAndOneTwentySeven)
{
	// 1 is the first class-A octet; 127 is the last (loopback range, but still classful math).
	EXPECT_TRUE(Is(Of(1, 2, 3, 4), 1, 255, 255, 255));
	EXPECT_TRUE(Is(Of(127, 0, 0, 1), 127, 255, 255, 255));
}

TEST(LanBroadcast, ClassBBoundaryOneTwentyEightAndOneNinetyOne)
{
	EXPECT_TRUE(Is(Of(128, 0, 0, 1), 128, 0, 255, 255));
	EXPECT_TRUE(Is(Of(191, 255, 3, 4), 191, 255, 255, 255));
}

TEST(LanBroadcast, ClassCBoundaryOneNinetyTwoAndTwoTwentyThree)
{
	EXPECT_TRUE(Is(Of(192, 0, 0, 1), 192, 0, 0, 255));
	EXPECT_TRUE(Is(Of(223, 255, 255, 1), 223, 255, 255, 255));
}

TEST(LanBroadcast, ZeroLeadingOctetFallsBackToLimited)
{
	EXPECT_TRUE(Is(Of(0, 0, 0, 0), 255, 255, 255, 255));
}

TEST(LanBroadcast, MulticastAndReservedFallBackToLimited)
{
	// 224 (multicast) and 240 (reserved) are past class C -- no directed broadcast.
	EXPECT_TRUE(Is(Of(224, 0, 0, 1), 255, 255, 255, 255));
	EXPECT_TRUE(Is(Of(240, 1, 2, 3), 255, 255, 255, 255));
	EXPECT_TRUE(Is(Of(255, 255, 255, 255), 255, 255, 255, 255));
}
