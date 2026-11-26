// [Doruk-Aksoy] Tests for actorlights_compute: idle only when nothing of the class or the state can light the actor.
#include <gtest/gtest.h>

#include "features/gldefs/computation/actorlights_compute.h"

using namespace zx::gldefs;

// [Doruk-Aksoy] An unlit class on an unlit state has nothing to attach or put out.
TEST(ComputeActorLightsIdle, UnlitClassAndStateIsIdle)
{
	EXPECT_TRUE(ComputeActorLightsIdle(false, 0, false));
	EXPECT_TRUE(ComputeActorLightsIdle(false, -1, false));
}

// [Doruk-Aksoy] Any one of associations, a state light or a light once given makes the call do its work.
TEST(ComputeActorLightsIdle, AnyLightSourceIsWork)
{
	EXPECT_FALSE(ComputeActorLightsIdle(true, 0, false));
	EXPECT_FALSE(ComputeActorLightsIdle(false, 1, false));
	EXPECT_FALSE(ComputeActorLightsIdle(false, 0, true));
	EXPECT_FALSE(ComputeActorLightsIdle(true, 3, true));
}
