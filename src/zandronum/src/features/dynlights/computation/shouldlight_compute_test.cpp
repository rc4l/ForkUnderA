// [Doruk-Aksoy] Tests for shouldlight_compute, each case UZDoom's ShouldLightActor answer for the same flags.
#include <gtest/gtest.h>

#include "features/dynlights/computation/shouldlight_compute.h"

using namespace zx::dynlights;

// [Doruk-Aksoy] A light with no flags lights its own actor and every other.
TEST(ComputeShouldLightActor, NoFlagsLightsEveryone)
{
	EXPECT_TRUE(ComputeShouldLightActor({ false, false, false }, true));
	EXPECT_TRUE(ComputeShouldLightActor({ false, false, false }, false));
}

// [Doruk-Aksoy] dontlightself skips only the actor that carries the light.
TEST(ComputeShouldLightActor, DontLightSelf)
{
	EXPECT_FALSE(ComputeShouldLightActor({ true, false, false }, true));
	EXPECT_TRUE(ComputeShouldLightActor({ true, false, false }, false));
}

// [Doruk-Aksoy] dontlightothers lights only the actor that carries the light.
TEST(ComputeShouldLightActor, DontLightOthers)
{
	EXPECT_TRUE(ComputeShouldLightActor({ false, true, false }, true));
	EXPECT_FALSE(ComputeShouldLightActor({ false, true, false }, false));
}

// [Doruk-Aksoy] dontlightactors lights no actor at all, its own included, whatever the other flags say.
TEST(ComputeShouldLightActor, DontLightActors)
{
	EXPECT_FALSE(ComputeShouldLightActor({ false, false, true }, true));
	EXPECT_FALSE(ComputeShouldLightActor({ false, false, true }, false));
	EXPECT_FALSE(ComputeShouldLightActor({ false, true, true }, true));
}

// [Doruk-Aksoy] dontlightself and dontlightothers together light nobody, as UZDoom's two clauses do.
TEST(ComputeShouldLightActor, SelfAndOthersTogether)
{
	EXPECT_FALSE(ComputeShouldLightActor({ true, true, false }, true));
	EXPECT_FALSE(ComputeShouldLightActor({ true, true, false }, false));
}
