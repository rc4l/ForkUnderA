// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/hwrender/computation/passname_compute.h"

using namespace zx::hwrender;

// [rc4l] WDI07's frame drew the world once per pass, and the stats had to say which pass cost what.
TEST(PassNameCompute, EachPassIsNamedByTheFirstFlagThatHolds)
{
	EXPECT_STREQ("main", ComputeWorldPassName(true, 5, 0, true, true));
	EXPECT_STREQ("mirror", ComputeWorldPassName(false, 5, 0, true, true));
	EXPECT_STREQ("stack", ComputeWorldPassName(false, -1, 0, true, true));
	EXPECT_STREQ("camera", ComputeWorldPassName(false, -1, -1, true, true));
	EXPECT_STREQ("sky", ComputeWorldPassName(false, -1, -1, false, true));
	EXPECT_STREQ("planemirror", ComputeWorldPassName(false, -1, -1, false, false));
}
