// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "features/acsvec/computation/acsvec_compute.h"
#include "features/actormaterials/computation/actormaterial_compute.h"

using namespace zx::acsvec;

namespace {

struct Golden
{
	int argCount;
	int32_t args[8];
	int count;
	int32_t value[3];
};

const Golden kGolden[] = {
#include "features/acsvec/computation/acsvec_golden.inc"
};

} // namespace

// [Doruk-Aksoy] Compiled mods hold these numbers, and zt-bcc's src/fua_vec.h pins the same ones.
TEST(AcsVec, TheNumbersNeverMove)
{
	EXPECT_EQ(zx::actormaterials::kSetActorUniform1i + 1, kVecFunction);
	EXPECT_EQ(24012, kVecFunction);
	EXPECT_EQ(0, kOpAbiVersion);
	EXPECT_EQ(1, kOpLength2);
	EXPECT_EQ(2, kOpLength3);
	EXPECT_EQ(3, kOpUnit2);
	EXPECT_EQ(4, kOpUnit3);
	EXPECT_EQ(5, kOpDot2);
	EXPECT_EQ(6, kOpDot3);
	EXPECT_EQ(7, kOpCross3);
	EXPECT_EQ(8, kOpCount);
	EXPECT_EQ(1, kAbiVersion);
}

// [Doruk-Aksoy] Every generated call gives the exactly rounded results tools/gen_acsvec_golden.py worked out apart from this code.
TEST(AcsVec, EveryCallMatchesTheExactResults)
{
	ASSERT_GT(sizeof(kGolden) / sizeof(kGolden[0]), size_t(800));
	for (const Golden& g : kGolden)
	{
		const VecCall call = ComputeVecCall(g.args, g.argCount);
		ASSERT_EQ(g.count, call.count) << "op " << g.args[g.argCount - 1];
		EXPECT_EQ(5, call.channel);
		for (int i = 0; i < g.count; ++i)
			EXPECT_EQ(g.value[i], call.value[i]) << "op " << g.args[g.argCount - 1] << " result " << i << " of " << g.args[0] << ", " << g.args[1];
	}
}

// [Doruk-Aksoy] The probe answers with the version whatever comes before it, and needs no channel.
TEST(AcsVec, TheProbeReturnsTheVersion)
{
	const int32_t alone[] = { kOpAbiVersion };
	VecCall call = ComputeVecCall(alone, 1);
	EXPECT_EQ(1, call.count);
	EXPECT_EQ(kAbiVersion, call.value[0]);
	const int32_t withChannel[] = { 9, kOpAbiVersion };
	EXPECT_EQ(kAbiVersion, ComputeVecCall(withChannel, 2).value[0]);
}

// [Doruk-Aksoy] No arguments, an unknown operation and the wrong number of operands are refused with no results.
TEST(AcsVec, MalformedCallsAreRefused)
{
	EXPECT_EQ(0, ComputeVecCall(nullptr, 0).count);
	const int32_t unknown[] = { 3, 4, 5, kOpCount };
	EXPECT_EQ(0, ComputeVecCall(unknown, 4).count);
	const int32_t negative[] = { 3, 4, 5, -1 };
	EXPECT_EQ(0, ComputeVecCall(negative, 4).count);
	const int32_t short3[] = { 3, 4, 5, kOpLength3 };
	EXPECT_EQ(0, ComputeVecCall(short3, 4).count);
	const int32_t long2[] = { 3, 4, 0, 5, kOpLength2 };
	EXPECT_EQ(0, ComputeVecCall(long2, 5).count);
}

// [Doruk-Aksoy] Each operation names how many operands it reads.
TEST(AcsVec, OperandCounts)
{
	EXPECT_EQ(0, ComputeVecOperandCount(kOpAbiVersion));
	EXPECT_EQ(2, ComputeVecOperandCount(kOpLength2));
	EXPECT_EQ(3, ComputeVecOperandCount(kOpLength3));
	EXPECT_EQ(2, ComputeVecOperandCount(kOpUnit2));
	EXPECT_EQ(3, ComputeVecOperandCount(kOpUnit3));
	EXPECT_EQ(4, ComputeVecOperandCount(kOpDot2));
	EXPECT_EQ(6, ComputeVecOperandCount(kOpDot3));
	EXPECT_EQ(6, ComputeVecOperandCount(kOpCross3));
	EXPECT_EQ(-1, ComputeVecOperandCount(kOpCount));
}

// [Doruk-Aksoy] The square root rounds to nearest, up to the largest 64-bit value.
TEST(AcsVec, SquareRootRoundsToNearest)
{
	EXPECT_EQ(0u, ComputeVecRoundSqrt(0));
	EXPECT_EQ(1u, ComputeVecRoundSqrt(1));
	EXPECT_EQ(1u, ComputeVecRoundSqrt(2));
	EXPECT_EQ(2u, ComputeVecRoundSqrt(3));
	EXPECT_EQ(2u, ComputeVecRoundSqrt(6));
	EXPECT_EQ(3u, ComputeVecRoundSqrt(7));
	EXPECT_EQ(3u, ComputeVecRoundSqrt(9));
	EXPECT_EQ(uint64_t(1) << 32, ComputeVecRoundSqrt(~uint64_t(0)));
}

// [Doruk-Aksoy] The familiar cases read as they should: a 3-4-5 triangle, axis unit vectors and the right-handed cross product.
TEST(AcsVec, FamiliarCases)
{
	const int32_t v[] = { 3 << 16, 4 << 16 };
	EXPECT_EQ(5 << 16, ComputeVecLength(v, 2));
	int32_t u[2];
	ComputeVecUnit(v, 2, u);
	EXPECT_EQ(39322, u[0]);
	EXPECT_EQ(52429, u[1]);
	const int32_t x[] = { 1 << 16, 0, 0 }, y[] = { 0, 1 << 16, 0 };
	int32_t z[3];
	ComputeVecCross(x, y, z);
	EXPECT_EQ(0, z[0]);
	EXPECT_EQ(0, z[1]);
	EXPECT_EQ(1 << 16, z[2]);
	const int32_t big[] = { 300 << 16, 300 << 16, 0 };
	EXPECT_EQ(kVecMax, ComputeVecDot(big, big, 3));
}
