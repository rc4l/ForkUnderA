// [rc4l] Tests for render-state resolution + key packing (hwrender).
#include "gtest/gtest.h"
#include "features/hwrender/computation/renderstate_compute.h"

using namespace hwrender;

namespace
{
TEST(RenderState, OpaqueDisablesBlend)
{
	BlendState s = ComputeBlendState(BlendMode::Opaque);
	EXPECT_FALSE(s.enabled);
	EXPECT_EQ(s.src, BlendFactor::One);
	EXPECT_EQ(s.dst, BlendFactor::Zero);
}

TEST(RenderState, TranslucentIsSrcAlphaOverInv)
{
	BlendState s = ComputeBlendState(BlendMode::Translucent);
	EXPECT_TRUE(s.enabled);
	EXPECT_EQ(s.src, BlendFactor::SrcAlpha);
	EXPECT_EQ(s.dst, BlendFactor::InvSrcAlpha);
	EXPECT_EQ(s.equation, BlendEquation::Add);
}

TEST(RenderState, AdditiveModulateSubtractive)
{
	EXPECT_EQ(ComputeBlendState(BlendMode::Additive).dst, BlendFactor::One);
	EXPECT_EQ(ComputeBlendState(BlendMode::Modulate).src, BlendFactor::DstColor);
	EXPECT_EQ(ComputeBlendState(BlendMode::Subtractive).equation, BlendEquation::ReverseSubtract);
}

TEST(RenderState, UnknownModeFallsBackToOpaque)
{
	BlendState s = ComputeBlendState((BlendMode)999);
	EXPECT_FALSE(s.enabled);
	EXPECT_EQ(s.src, BlendFactor::One);
	EXPECT_EQ(s.dst, BlendFactor::Zero);
}

TEST(RenderState, StateKeyPacksFieldsAndRoundTrips)
{
	uint64_t k = ComputeStateKey(0x1234, 0xABCD, BlendMode::Additive, true, false);
	EXPECT_EQ((k >> 48) & 0xFFFF, 0x1234u);          // shader
	EXPECT_EQ((k >> 32) & 0xFFFF, 0xABCDu);          // texture
	EXPECT_EQ((k >> 24) & 0xFF, (uint8_t)BlendMode::Additive);
	EXPECT_EQ((k >> 1) & 1, 1u);                     // depthTest
	EXPECT_EQ(k & 1, 0u);                            // cull
}

TEST(RenderState, StateKeyIsStableAndDistinct)
{
	uint64_t a = ComputeStateKey(1, 2, BlendMode::Opaque, true, true);
	uint64_t b = ComputeStateKey(1, 2, BlendMode::Opaque, true, true);
	uint64_t c = ComputeStateKey(1, 2, BlendMode::Opaque, true, false);
	EXPECT_EQ(a, b);   // same inputs -> same key (batchable)
	EXPECT_NE(a, c);   // differing cull -> different key
}
} // namespace
