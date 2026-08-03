// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Unit tests for the pure runtime-resize helpers (features/actorresize/actorresize.h).
#include "features/actorresize/actorresize.h"

#include <gtest/gtest.h>

namespace {

using ActorResize::ComputeResolvedDimension;
using ActorResize::ComputeSizeDelta;
using ActorResize::SizeDelta;

// [rc4l] long long stands in for the engine's 64-bit fixed_t; values are raw fixed-point.
using fx = long long;

TEST(ActorResizeResolvedDimension, NegativeKeepsCurrent) {
  // [rc4l] A_SetSize's -1 sentinel (and any negative) keeps the current dimension.
  EXPECT_EQ(ComputeResolvedDimension<fx>(-1, 56 << 16), 56 << 16);
  EXPECT_EQ(ComputeResolvedDimension<fx>(-65536, 100 << 16), 100 << 16);
}

TEST(ActorResizeResolvedDimension, NonNegativeUsesRequest) {
  // [rc4l] Zero and positive requests are applied verbatim (0 is a valid degenerate size).
  EXPECT_EQ(ComputeResolvedDimension<fx>(0, 56 << 16), 0);
  EXPECT_EQ(ComputeResolvedDimension<fx>(72 << 16, 20 << 16), 72 << 16);
}

TEST(ActorResizeSizeDelta, FlagsMatchPerDimensionChange) {
  // [rc4l] Each flag tracks its own dimension independently.
  const SizeDelta both = ComputeSizeDelta<fx>(20, 72, 56, 100);
  EXPECT_TRUE(both.radiusChanged);
  EXPECT_TRUE(both.heightChanged);

  const SizeDelta radiusOnly = ComputeSizeDelta<fx>(20, 72, 56, 56);
  EXPECT_TRUE(radiusOnly.radiusChanged);
  EXPECT_FALSE(radiusOnly.heightChanged);

  const SizeDelta heightOnly = ComputeSizeDelta<fx>(20, 20, 56, 100);
  EXPECT_FALSE(heightOnly.radiusChanged);
  EXPECT_TRUE(heightOnly.heightChanged);

  const SizeDelta unchanged = ComputeSizeDelta<fx>(20, 20, 56, 56);
  EXPECT_FALSE(unchanged.radiusChanged);
  EXPECT_FALSE(unchanged.heightChanged);
}

TEST(ActorResizeSizeDelta, AnyReflectsEitherDimension) {
  // [rc4l] Any() gates the broadcast; cover both operands of the OR and the false case.
  EXPECT_FALSE(ComputeSizeDelta<fx>(20, 20, 56, 56).Any());  // nothing changed
  EXPECT_TRUE(ComputeSizeDelta<fx>(20, 72, 56, 56).Any());   // radius only (left operand)
  EXPECT_TRUE(ComputeSizeDelta<fx>(20, 20, 56, 100).Any());  // height only (right operand)
  EXPECT_TRUE(ComputeSizeDelta<fx>(20, 72, 56, 100).Any());  // both
}

}  // namespace
