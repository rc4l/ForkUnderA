// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "features/decorate-uzdoom/computation/uservar_compute.h"

using namespace zx::decorateuzdoom;

TEST(UserVarAccess, ShapesAndBounds)
{
	EXPECT_EQ(ComputeUserVarAccess(true, false, 1, kUserVarScalar, 0), kUserVarFound);
	EXPECT_EQ(ComputeUserVarAccess(true, true, 4, kUserVarScalar, 0), kUserVarNotDeclared);
	EXPECT_EQ(ComputeUserVarAccess(true, false, 1, kUserVarArray, 0), kUserVarNotDeclared);
	EXPECT_EQ(ComputeUserVarAccess(true, true, 4, kUserVarArray, 3), kUserVarFound);
	EXPECT_EQ(ComputeUserVarAccess(true, true, 4, kUserVarArray, 4), kUserVarOutOfBounds);
	EXPECT_EQ(ComputeUserVarAccess(true, true, 4, kUserVarArray, -1), kUserVarOutOfBounds);
	EXPECT_EQ(ComputeUserVarAccess(true, false, 1, kUserVarEither, 1), kUserVarOutOfBounds);
	EXPECT_EQ(ComputeUserVarAccess(true, true, 4, kUserVarEither, 2), kUserVarFound);
	EXPECT_EQ(ComputeUserVarAccess(false, false, 1, kUserVarEither, 0), kUserVarNotDeclared);
}

TEST(UserVarLayout, IntsAppendAtFourBytesEach)
{
	const UserVarLayout one = ComputeUserVarLayout(100, false, 1);
	EXPECT_EQ(one.offset, 100u);
	EXPECT_EQ(one.extension, 4u);
	const UserVarLayout array = ComputeUserVarLayout(100, false, 5);
	EXPECT_EQ(array.offset, 100u);
	EXPECT_EQ(array.extension, 20u);
}

TEST(UserVarLayout, FloatsAlignToEightBytes)
{
	const UserVarLayout aligned = ComputeUserVarLayout(96, true, 1);
	EXPECT_EQ(aligned.offset, 96u);
	EXPECT_EQ(aligned.extension, 8u);
	const UserVarLayout padded = ComputeUserVarLayout(100, true, 3);
	EXPECT_EQ(padded.offset, 104u);
	EXPECT_EQ(padded.extension, 28u);
}

TEST(UserVarConvert, DoubleIntoIntTruncatesTowardZero)
{
	EXPECT_EQ(ComputeUserVarIntFromDouble(2.9), 2);
	EXPECT_EQ(ComputeUserVarIntFromDouble(-2.9), -2);
	EXPECT_EQ(ComputeUserVarIntFromDouble(1e12), INT32_MAX);
	EXPECT_EQ(ComputeUserVarIntFromDouble(-1e12), INT32_MIN);
	EXPECT_EQ(ComputeUserVarIntFromDouble(std::nan("")), INT32_MIN);
}

TEST(UserVarConvert, AcsRoundTrip)
{
	EXPECT_EQ(ComputeUserVarToAcs(1.5), 0x18000);
	EXPECT_EQ(ComputeUserVarToAcs(-0.25), -0x4000);
	EXPECT_EQ(ComputeUserVarFromAcs(0x18000), 1.5);
	EXPECT_EQ(ComputeUserVarFromAcs(-0x4000), -0.25);
}
