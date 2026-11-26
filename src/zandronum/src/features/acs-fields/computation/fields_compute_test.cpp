// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "features/acs-fields/computation/fields_compute.h"

using namespace zx::acsfields;

namespace {

constexpr int32_t kOne = 0x10000;

} // namespace

TEST(AcsFields, ShapesFollowTheDecorateType)
{
	EXPECT_EQ(ComputeFieldShape(kValInt, 0, 0, false).kind, kFieldInt);
	EXPECT_FALSE(ComputeFieldShape(kValInt, 0, 0, false).writable);
	EXPECT_TRUE(ComputeFieldShape(kValInt, 0, 0, true).writable);
	EXPECT_EQ(ComputeFieldShape(kValFixed, 0, 0, false).kind, kFieldFixed);
	EXPECT_EQ(ComputeFieldShape(kValAngle, 0, 0, false).kind, kFieldAngle);
	EXPECT_EQ(ComputeFieldShape(kValBool, 0, 0, false).kind, kFieldBool);
	EXPECT_EQ(ComputeFieldShape(kValFloat, 0, 0, false).kind, kFieldFloat);
	EXPECT_FALSE(ComputeFieldShape(kValFixed, 0, 0, true).writable);
	const FieldShape none = ComputeFieldShape(7, 0, 0, true);
	EXPECT_EQ(none.kind, kFieldNone);
	EXPECT_EQ(none.count, 0);
	EXPECT_FALSE(none.writable);
}

TEST(AcsFields, ArraysTakeTheirBaseTypeAndLength)
{
	const FieldShape args = ComputeFieldShape(kValArray, kValInt, 5, false);
	EXPECT_EQ(args.kind, kFieldInt);
	EXPECT_EQ(args.count, 5);
	EXPECT_FALSE(args.writable);
	EXPECT_TRUE(ComputeFieldShape(kValArray, kValInt, 3, true).writable);
	EXPECT_EQ(ComputeFieldShape(kValArray, kValInt, 0, true).kind, kFieldNone);
	EXPECT_EQ(ComputeFieldShape(kValArray, 9, 4, true).count, 0);
	EXPECT_TRUE(ComputeFieldIndexOk(args, 4));
	EXPECT_FALSE(ComputeFieldIndexOk(args, 5));
	EXPECT_FALSE(ComputeFieldIndexOk(args, -1));
	EXPECT_FALSE(ComputeFieldIndexOk(ComputeFieldShape(7, 0, 0, false), 0));
}

// [Doruk-Aksoy] As GetActorX does, a fixed_t past ±32767 units keeps only its low 32 bits.
TEST(AcsFields, FixedValuesTruncateLikeGetActorX)
{
	EXPECT_EQ(ComputeFixedToAcs(int64_t(100) * kOne), 100 * kOne);
	EXPECT_EQ(ComputeFixedToAcs(int64_t(-3) * kOne / 2), -3 * kOne / 2);
	EXPECT_EQ(ComputeFixedToAcs(int64_t(40000) * kOne), static_cast<int32_t>(uint32_t(40000) << 16));
	EXPECT_EQ(ComputeFixedToAcs(int64_t(65536) * kOne), 0);
}

// [Doruk-Aksoy] As GetActorAngle does, a quarter turn reads 0.25.
TEST(AcsFields, AnglesReadAsFractionsOfATurn)
{
	EXPECT_EQ(ComputeAngleToAcs(0x40000000u), kOne / 4);
	EXPECT_EQ(ComputeAngleToAcs(0xC0000000u), kOne * 3 / 4);
	EXPECT_EQ(ComputeAngleToAcs(0), 0);
}

TEST(AcsFields, DoublesBecomeFixedPoint)
{
	EXPECT_EQ(ComputeFloatToAcs(1.5), kOne * 3 / 2);
	EXPECT_EQ(ComputeFloatToAcs(-0.25), -kOne / 4);
	EXPECT_EQ(ComputeFloatToAcs(std::nan("")), 0);
	EXPECT_EQ(ComputeFloatToAcs(1e300), 0);
	EXPECT_EQ(ComputeFloatToAcs(-1e300), 0);
}

TEST(AcsFields, EachKindReadsFromItsStorage)
{
	const int32_t ints[3] = { 7, -8, 9 };
	const int64_t fixeds[2] = { int64_t(5) * kOne, int64_t(70000) * kOne };
	const uint32_t angles[1] = { 0x80000000u };
	const bool flags[2] = { false, true };
	const double reals[1] = { 2.0 };
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(kValArray, kValInt, 3, false), ints, 1), -8);
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(kValArray, kValFixed, 2, false), fixeds, 0), 5 * kOne);
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(kValArray, kValFixed, 2, false), fixeds, 1), ComputeFixedToAcs(fixeds[1]));
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(kValAngle, 0, 0, false), angles, 0), kOne / 2);
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(kValArray, kValBool, 2, false), flags, 1), 1);
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(kValArray, kValBool, 2, false), flags, 0), 0);
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(kValFloat, 0, 0, false), reals, 0), 2 * kOne);
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(kValInt, 0, 0, false), ints, 1), 0);
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(kValInt, 0, 0, false), nullptr, 0), 0);
	EXPECT_EQ(ComputeFieldRead(ComputeFieldShape(7, 0, 0, false), ints, 0), 0);
}

// [Doruk-Aksoy] Only an int user_ variable or array takes a write, and only within its length.
TEST(AcsFields, OnlyUserIntsTakeWrites)
{
	int32_t user[3] = { 0, 0, 0 };
	const FieldShape array = ComputeFieldShape(kValArray, kValInt, 3, true);
	EXPECT_TRUE(ComputeFieldWrite(array, user, 2, 42));
	EXPECT_EQ(user[2], 42);
	EXPECT_FALSE(ComputeFieldWrite(array, user, 3, 1));
	EXPECT_FALSE(ComputeFieldWrite(array, nullptr, 0, 1));
	EXPECT_FALSE(ComputeFieldWrite(ComputeFieldShape(kValInt, 0, 0, false), user, 0, 5));
	EXPECT_EQ(user[0], 0);
}
