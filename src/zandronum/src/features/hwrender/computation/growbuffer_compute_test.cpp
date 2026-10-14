// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/hwrender/computation/growbuffer_compute.h"

using namespace zx::hwrender;

TEST(GrowBufferCompute, AShortBufferGrowsByHalfAgainPlusItsPad)
{
	EXPECT_EQ(1024u, ComputeGrownCapacity(0, 1024));
	EXPECT_EQ(150u + 256u, ComputeGrownCapacity(100, 256));
	EXPECT_EQ(4u, ComputeGrownCapacity(3, 0));   // [rc4l] the half rounds down
}
