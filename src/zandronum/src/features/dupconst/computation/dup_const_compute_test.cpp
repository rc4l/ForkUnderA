// [rc4l] Tests for the duplicate-DECORATE-constant policy.
//
// The load-breaking bug this guards: a mod defining a constant the engine also ships (MM8BDM's
// STYLE_*) aborted the entire game. The policy must tolerate that case and ONLY that case -- a
// class-scoped duplicate, or a collision with a non-constant, must still fail loudly.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"
#include "features/dupconst/computation/dup_const_compute.h"

using namespace zx;

TEST(DuplicateConstant, GlobalConstantCollisionIsToleratedNotFatal)
{
	// The MM8BDM case: mod redefines STYLE_Translucent as 64 where the engine says 6. Engine wins,
	// and the differing value must be reported as consequential rather than shrugged off.
	EXPECT_EQ(ComputeDuplicateConstantAction(true, true, false), DupConst_WarnValueChanged);
	// The BT_*/JIF_* case: the mod's value agrees, so nothing can change.
	EXPECT_EQ(ComputeDuplicateConstantAction(true, true, true), DupConst_WarnSameValue);
}

TEST(DuplicateConstant, ClassScopedDuplicateStaysAnError)
{
	// A mod defining the same const twice inside one actor is contradicting itself; tolerating that
	// would hide an authoring bug rather than fix a compatibility problem.
	EXPECT_EQ(ComputeDuplicateConstantAction(false, true, true), DupConst_Error);
	EXPECT_EQ(ComputeDuplicateConstantAction(false, true, false), DupConst_Error);
}

TEST(DuplicateConstant, CollisionWithANonConstantStaysAnError)
{
	// The name is already a native variable/function, not a duplicated value -- dropping the new
	// definition would silently change what the name refers to.
	EXPECT_EQ(ComputeDuplicateConstantAction(true, false, true), DupConst_Error);
	EXPECT_EQ(ComputeDuplicateConstantAction(true, false, false), DupConst_Error);
	// Non-constant collision outranks scope: still an error at class scope too.
	EXPECT_EQ(ComputeDuplicateConstantAction(false, false, false), DupConst_Error);
}

TEST(DuplicateConstant, OnlyTheGlobalConstantCaseIsEverTolerated)
{
	// Exhaustive over the three flags: exactly the two global+constant rows may be non-fatal.
	for (int g = 0; g < 2; ++g)
		for (int c = 0; c < 2; ++c)
			for (int s = 0; s < 2; ++s)
			{
				const DuplicateConstantAction got =
					ComputeDuplicateConstantAction(g != 0, c != 0, s != 0);
				if (g && c)
					EXPECT_NE(got, DupConst_Error) << "g=" << g << " c=" << c << " s=" << s;
				else
					EXPECT_EQ(got, DupConst_Error) << "g=" << g << " c=" << c << " s=" << s;
			}
}
