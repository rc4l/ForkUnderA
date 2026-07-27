// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

// [MGOOOOOO] Unit tests for the attack-extent fallback (src/p_attackextent.h).
#include "p_attackextent.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

// [MGOOOOOO] fixed_t is a 64-bit signed integer in this fork, so exercise the helper with int64_t.
using ext_t = std::int64_t;

TEST(AttackExtent, PositiveOverrideWiderThanPhysicalWins) {
  EXPECT_EQ(ComputeAttackExtent<ext_t>(64, 20), 64);
}

TEST(AttackExtent, PositiveOverrideNarrowerThanPhysicalWins) {
  EXPECT_EQ(ComputeAttackExtent<ext_t>(8, 20), 8);
}

TEST(AttackExtent, ZeroFallsBackToPhysical) {
  EXPECT_EQ(ComputeAttackExtent<ext_t>(0, 20), 20);
}

TEST(AttackExtent, NegativeFallsBackToPhysical) {
  EXPECT_EQ(ComputeAttackExtent<ext_t>(-16, 20), 20);
}

// ComputeAttackHeight: crouch-scaling of a custom PassHeight.

TEST(AttackHeight, NoOverrideReturnsPhysicalHeight) {
  // passHeight <= 0 -> use the physical height (which already reflects any crouch).
  EXPECT_EQ(ComputeAttackHeight<ext_t>(0, 28, 56), 28);
  EXPECT_EQ(ComputeAttackHeight<ext_t>(-16, 28, 56), 28);
}

TEST(AttackHeight, StandingKeepsFullOverride) {
  // physical == default (not crouched) -> full override.
  EXPECT_EQ(ComputeAttackHeight<ext_t>(96, 56, 56), 96);
}

TEST(AttackHeight, CrouchScalesOverrideDown) {
  // Half-crouched (physical 28 of default 56) halves the override: 96 * 28 / 56 = 48.
  EXPECT_EQ(ComputeAttackHeight<ext_t>(96, 28, 56), 48);
}

TEST(AttackHeight, TallerThanDefaultDoesNotInflate) {
  // physical > default -> never inflate the override.
  EXPECT_EQ(ComputeAttackHeight<ext_t>(96, 80, 56), 96);
}

TEST(AttackHeight, ZeroDefaultGuardKeepsOverride) {
  // defaultHeight <= 0 guard: no scaling.
  EXPECT_EQ(ComputeAttackHeight<ext_t>(96, 28, 0), 96);
}

// AttackHitboxIsEnlarged: gate for the anti-bleed line-of-sight check.

TEST(HitboxEnlarged, EqualBoxIsNotEnlarged) {
  EXPECT_FALSE(AttackHitboxIsEnlarged<ext_t>(20, 20, 56, 56));
}

TEST(HitboxEnlarged, SmallerBoxIsNotEnlarged) {
  EXPECT_FALSE(AttackHitboxIsEnlarged<ext_t>(8, 20, 40, 56));
}

TEST(HitboxEnlarged, WiderRadiusIsEnlarged) {
  EXPECT_TRUE(AttackHitboxIsEnlarged<ext_t>(96, 20, 56, 56));
}

TEST(HitboxEnlarged, TallerHeightIsEnlarged) {
  EXPECT_TRUE(AttackHitboxIsEnlarged<ext_t>(20, 20, 96, 56));
}

TEST(HitboxEnlarged, NarrowRadiusButTallerHeightIsEnlarged) {
  // Enlarged in either dimension is enough to require the check.
  EXPECT_TRUE(AttackHitboxIsEnlarged<ext_t>(8, 20, 96, 56));
}

}  // namespace
