// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/crashreport/computation/fault_frame_compute.h"

#include <gtest/gtest.h>

namespace {

TEST(ComputeFaultFrame, AnOrdinaryFaultWalksFromWhereItIs)
{
	const zx::FaultFrame f = zx::ComputeFaultFrame(0x140001234ull, 0x7ff0ull, 0xdeadull);
	EXPECT_EQ(f.pc, 0x140001234ull);
	EXPECT_EQ(f.sp, 0x7ff0ull);
}

TEST(ComputeFaultFrame, ANullCallWalksFromItsCallerWithTheReturnAddressPopped)
{
	// [rc4l] An unloaded GL entry point is a call to 0: the program counter says nothing, the slot at sp says who called.
	const zx::FaultFrame f = zx::ComputeFaultFrame(0, 0x7ff0ull, 0x140005678ull);
	EXPECT_EQ(f.pc, 0x140005678ull);
	EXPECT_EQ(f.sp, 0x7ff8ull);
}

} // namespace
