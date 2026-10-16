// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/serial_compute.h"

#include <gtest/gtest.h>

using zx::gamecopy::ComputeNextSerial;
using zx::gamecopy::kNoId;

TEST(Serial, CountsUp)
{
	EXPECT_EQ(1u, ComputeNextSerial(kNoId));
	EXPECT_EQ(43u, ComputeNextSerial(42));
}

TEST(Serial, WrapsPastNoActor)
{
	EXPECT_EQ(1u, ComputeNextSerial(0xffffffffu));
}
