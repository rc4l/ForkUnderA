// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <cstddef>

#include "features/actormaterials/computation/actormaterial_compute.h"

using namespace zx::actormaterials;
using zx::acsshaders::UniformValue;

// [Doruk-Aksoy] Our numbers follow the shader functions' and nothing else is ours.
TEST(ActorMaterial, TheFunctionsFollowTheShaderOnes)
{
	EXPECT_EQ(zx::acsshaders::kSetShaderUniform1i + 1, kSetActorMaterial);
	EXPECT_TRUE(ComputeIsActorMaterialFunction(kSetActorMaterial));
	EXPECT_TRUE(ComputeIsActorMaterialFunction(kSetActorUniform1i));
	EXPECT_FALSE(ComputeIsActorMaterialFunction(kSetActorMaterial - 1));
	EXPECT_FALSE(ComputeIsActorMaterialFunction(kSetActorUniform1i + 1));
}

// [Doruk-Aksoy] A call reads its tid, the uniform's string and its values, the floats from 16.16, and is refused short of them or for another function.
TEST(ActorMaterial, AUniformCallReadsItsArguments)
{
	const int f3[] = { 7, 12, 65536, 32768, 0 };
	ActorUniformCall c = ComputeActorUniformCall(kSetActorUniform3f, f3, 5);
	ASSERT_TRUE(c.ok);
	EXPECT_EQ(7, c.tid);
	EXPECT_EQ(12, c.uniform);
	EXPECT_EQ(3, c.value.kind);
	EXPECT_DOUBLE_EQ(0.5, c.value.values[1]);
	EXPECT_FALSE(ComputeActorUniformCall(kSetActorUniform3f, f3, 4).ok);
	const int i1[] = { 0, 3, 9 };
	c = ComputeActorUniformCall(kSetActorUniform1i, i1, 3);
	ASSERT_TRUE(c.ok);
	EXPECT_EQ(0, c.value.kind);
	EXPECT_DOUBLE_EQ(9.0, c.value.values[0]);
	EXPECT_FALSE(ComputeActorUniformCall(kSetActorUniform1i, i1, 2).ok);
	EXPECT_FALSE(ComputeActorUniformCall(kSetActorMaterial, i1, 3).ok);
	EXPECT_FALSE(ComputeActorUniformCall(kSetActorUniform1i + 1, i1, 3).ok);
}

// [Doruk-Aksoy] Slots are reused first, bounded, given back once, and every change and no other bumps the generation.
TEST(ActorMaterial, SlotsAreReusedAndBounded)
{
	InstanceTable t;
	EXPECT_EQ(0, ComputeAcquireInstance(t, 2));
	EXPECT_EQ(1, ComputeAcquireInstance(t, 3));
	const unsigned g = t.generation;
	ComputeReleaseInstance(t, 0);
	EXPECT_EQ(g + 1, t.generation);
	ComputeReleaseInstance(t, 0);
	ComputeReleaseInstance(t, -1);
	ComputeReleaseInstance(t, 9);
	EXPECT_EQ(g + 1, t.generation);
	EXPECT_EQ(0, ComputeAcquireInstance(t, 4));
	EXPECT_EQ(4, t.instances[0].material);
	t.instances.resize(kMaxActorInstances);
	for (ActorInstance &a : t.instances) a.used = true;
	EXPECT_EQ(-1, ComputeAcquireInstance(t, 1));
	ComputeClearInstances(t);
	EXPECT_TRUE(t.instances.empty());
	const unsigned cleared = t.generation;
	ComputeClearInstances(t);
	EXPECT_EQ(cleared, t.generation);
}

// [Doruk-Aksoy] A uniform set again to what it holds changes nothing; one out of range, or on a slot not in use, is refused.
TEST(ActorMaterial, AUniformChangesOnlyWhenItDiffers)
{
	InstanceTable t;
	const int s = ComputeAcquireInstance(t, 0);
	UniformValue v = { 2, { 0.25, 0.5, 0, 0 } };
	EXPECT_TRUE(ComputeSetInstanceUniform(t, s, 1, v));
	EXPECT_FLOAT_EQ(0.5f, t.instances[s].values[1][1]);
	EXPECT_FLOAT_EQ(1.f, t.instances[s].values[1][3]);
	EXPECT_FALSE(ComputeSetInstanceUniform(t, s, 1, v));
	v.values[0] = 1;
	EXPECT_TRUE(ComputeSetInstanceUniform(t, s, 1, v));
	EXPECT_FALSE(ComputeSetInstanceUniform(t, s, kActorUniformVec4s, v));
	EXPECT_FALSE(ComputeSetInstanceUniform(t, s, -1, v));
	EXPECT_FALSE(ComputeSetInstanceUniform(t, s + 1, 0, v));
	EXPECT_FALSE(ComputeSetInstanceUniform(t, -1, 0, v));
	ComputeReleaseInstance(t, s);
	EXPECT_FALSE(ComputeSetInstanceUniform(t, s, 0, v));
}

// [Doruk-Aksoy] A line names the actor and material, then each uniform at its own value where set, else its default, and zeros past the defaults.
TEST(ActorMaterial, ALineShowsOwnValuesOverDefaults)
{
	const MaterialDefaults d = { { 1, 0, 0, 1 } };
	EXPECT_EQ("tid=9 glow tint=1,0,0,1 swap=0,0,0,0", ComputeActorUniformLine(9, "glow", { "tint", "swap" }, d, nullptr));
	InstanceTable t;
	const int s = ComputeAcquireInstance(t, 0);
	ComputeSetInstanceUniform(t, s, 0, UniformValue{ 3, { 0, 1, 0, 0 } });
	EXPECT_EQ("tid=9 glow tint=0,1,0,1 swap=0,0,0,0", ComputeActorUniformLine(9, "glow", { "tint", "swap" }, d, &t.instances[s]));
	EXPECT_EQ("tid=2 bare", ComputeActorUniformLine(2, "bare", {}, {}, nullptr));
}

// [Doruk-Aksoy] Materials' rows come first, then a slot's, whose unset uniforms read its material's defaults and whose freed slot reads zeros.
TEST(ActorMaterial, RowsTakeOwnValuesOverDefaults)
{
	EXPECT_EQ(3, ComputeActorUniformRow(3, -1, 5));
	EXPECT_EQ(7, ComputeActorUniformRow(3, 2, 5));
	const std::vector<MaterialDefaults> defaults = { { { 1, 2, 3, 4 } }, { { 5, 6, 7, 8 }, { 9, 9, 9, 9 } } };
	InstanceTable t;
	const int a = ComputeAcquireInstance(t, 1);
	ComputeAcquireInstance(t, 7);
	const int c = ComputeAcquireInstance(t, 0);
	ComputeReleaseInstance(t, c);
	ComputeSetInstanceUniform(t, a, 1, UniformValue{ 0, { 42, 0, 0, 0 } });
	std::vector<float> out;
	ComputeActorUniformRows(defaults, t, out);
	const size_t row = kActorUniformVec4s * 4;
	ASSERT_EQ(5 * row, out.size());
	EXPECT_FLOAT_EQ(4.f, out[3]);
	EXPECT_FLOAT_EQ(0.f, out[4]);
	EXPECT_FLOAT_EQ(9.f, out[row + 4]);
	EXPECT_FLOAT_EQ(5.f, out[2 * row]);
	EXPECT_FLOAT_EQ(42.f, out[2 * row + 4]);
	EXPECT_FLOAT_EQ(1.f, out[2 * row + 7]);
	EXPECT_FLOAT_EQ(0.f, out[3 * row]);
	EXPECT_FLOAT_EQ(0.f, out[4 * row]);
}
