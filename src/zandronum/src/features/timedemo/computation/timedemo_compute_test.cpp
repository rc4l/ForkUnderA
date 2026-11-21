// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/timedemo/computation/timedemo_compute.h"
#include <gtest/gtest.h>

// [Doruk-Aksoy] The line the old fatal error carried, read by tools that time demos.
TEST(ComputeTimedemoLine, AsTheOldFatalErrorWorded)
{
	EXPECT_EQ(ComputeTimedemoLine(2953, 2330, 35), "timed 2953 gametics in 2330 realtics (44.4 fps)\n");
}

// [Doruk-Aksoy] A demo timed in no real tics reads 0 fps rather than dividing by zero.
TEST(ComputeTimedemoLine, NoRealTics)
{
	EXPECT_EQ(ComputeTimedemoLine(10, 0, 35), "timed 10 gametics in 0 realtics (0.0 fps)\n");
}
