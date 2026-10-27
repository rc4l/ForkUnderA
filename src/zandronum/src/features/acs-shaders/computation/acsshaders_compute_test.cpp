// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/acs-shaders/computation/acsshaders_compute.h"

using namespace zx::acsshaders;
using zx::postprocess::ShaderTable;

// [Doruk-Aksoy] The block is 24000 to 24005, and nothing beside it is ours.
TEST(AcsShaders, TheFunctionNumbersAreTheReservedBlock)
{
	EXPECT_EQ(24000, kSetShaderEnabled);
	EXPECT_EQ(24005, kSetShaderUniform1i);
	EXPECT_TRUE(ComputeIsShaderFunction(24000));
	EXPECT_TRUE(ComputeIsShaderFunction(24005));
	EXPECT_FALSE(ComputeIsShaderFunction(23999));
	EXPECT_FALSE(ComputeIsShaderFunction(24006));
}

// [Doruk-Aksoy] For everyone: a server records it and sends it to all, offline and a client only apply it.
TEST(AcsShaders, ForEveryoneTheServerSendsToAll)
{
	Route r = ComputeShaderRoute(kServer, false, 2, -1);
	EXPECT_TRUE(r.apply);
	EXPECT_EQ(-1, r.player);
	EXPECT_EQ(kSendAll, r.sendTo);
	for (const Role role : { kOffline, kClient })
	{
		r = ComputeShaderRoute(role, false, kActivatorNone, 0);
		EXPECT_TRUE(r.apply);
		EXPECT_EQ(-1, r.player);
		EXPECT_EQ(kSendNone, r.sendTo);
	}
}

// [Doruk-Aksoy] For the activator only: a server sends to that player's client alone, and nowhere for a monster or the world.
TEST(AcsShaders, ForTheActivatorTheServerSendsToItsClient)
{
	Route r = ComputeShaderRoute(kServer, true, 3, -1);
	EXPECT_TRUE(r.apply);
	EXPECT_EQ(3, r.player);
	EXPECT_EQ(3, r.sendTo);
	EXPECT_FALSE(ComputeShaderRoute(kServer, true, kActivatorNotPlayer, -1).apply);
	EXPECT_FALSE(ComputeShaderRoute(kServer, true, kActivatorNone, -1).apply);
}

// [Doruk-Aksoy] Locally the activator only counts when it is the console player, the world counting as them.
TEST(AcsShaders, LocallyOnlyTheConsolePlayersActivatorApplies)
{
	for (const Role role : { kOffline, kClient })
	{
		Route r = ComputeShaderRoute(role, true, 1, 1);
		EXPECT_TRUE(r.apply);
		EXPECT_EQ(1, r.player);
		EXPECT_EQ(kSendNone, r.sendTo);
		r = ComputeShaderRoute(role, true, kActivatorNone, 1);
		EXPECT_TRUE(r.apply);
		EXPECT_EQ(1, r.player);
		EXPECT_FALSE(ComputeShaderRoute(role, true, 2, 1).apply);
		EXPECT_FALSE(ComputeShaderRoute(role, true, kActivatorNotPlayer, 1).apply);
	}
}

// [Doruk-Aksoy] The enable's arguments, each optional one defaulting off, a negative duration being none.
TEST(AcsShaders, EnableArgumentsAndTheirDefaults)
{
	const int full[] = { 7, 1, 1, 105 };
	EnableCall c = ComputeEnableCall(full, 4);
	EXPECT_TRUE(c.ok);
	EXPECT_TRUE(c.enable);
	EXPECT_TRUE(c.activatorOnly);
	EXPECT_EQ(105, c.durationTics);
	const int bare[] = { 7, 0 };
	c = ComputeEnableCall(bare, 2);
	EXPECT_TRUE(c.ok);
	EXPECT_FALSE(c.enable);
	EXPECT_FALSE(c.activatorOnly);
	EXPECT_EQ(0, c.durationTics);
	const int negative[] = { 7, 1, 0, -5 };
	EXPECT_EQ(0, ComputeEnableCall(negative, 4).durationTics);
	EXPECT_FALSE(ComputeEnableCall(full, 1).ok);
}

// [Doruk-Aksoy] Each uniform function takes its count of fixed-point floats or one int, then the optional activator flag.
TEST(AcsShaders, UniformArgumentsByFunction)
{
	const int f3[] = { 1, 2, 65536, 32768, -98304, 1 };
	UniformCall c = ComputeUniformCall(kSetShaderUniform3f, f3, 6);
	EXPECT_TRUE(c.ok);
	EXPECT_EQ(3, c.value.kind);
	EXPECT_DOUBLE_EQ(1.0, c.value.values[0]);
	EXPECT_DOUBLE_EQ(0.5, c.value.values[1]);
	EXPECT_DOUBLE_EQ(-1.5, c.value.values[2]);
	EXPECT_TRUE(c.activatorOnly);
	const int i1[] = { 1, 2, 123456789 };
	c = ComputeUniformCall(kSetShaderUniform1i, i1, 3);
	EXPECT_TRUE(c.ok);
	EXPECT_EQ(0, c.value.kind);
	EXPECT_DOUBLE_EQ(123456789.0, c.value.values[0]);
	EXPECT_FALSE(c.activatorOnly);
	EXPECT_FALSE(ComputeUniformCall(kSetShaderUniform4f, f3, 5).ok);
	EXPECT_FALSE(ComputeUniformCall(kSetShaderEnabled, f3, 6).ok);
	EXPECT_EQ(1, ComputeUniformCall(kSetShaderUniform1f, f3, 3).value.kind);
	EXPECT_EQ(2, ComputeUniformCall(kSetShaderUniform2f, f3, 4).value.kind);
	EXPECT_EQ(4, ComputeUniformCall(kSetShaderUniform4f, f3, 6).value.kind);
}

// [Doruk-Aksoy] A value fills its own components and leaves the rest 0 with w 1, as UZDoom's console does.
TEST(AcsShaders, AValueFillsItsComponents)
{
	double out[4];
	ComputeUniformValues({ 2, { 0.25, 0.75, 9.0, 9.0 } }, out);
	EXPECT_DOUBLE_EQ(0.25, out[0]);
	EXPECT_DOUBLE_EQ(0.75, out[1]);
	EXPECT_DOUBLE_EQ(0.0, out[2]);
	EXPECT_DOUBLE_EQ(1.0, out[3]);
	ComputeUniformValues({ 0, { 42.0, 9.0, 9.0, 9.0 } }, out);
	EXPECT_DOUBLE_EQ(42.0, out[0]);
	EXPECT_DOUBLE_EQ(0.0, out[1]);
	ComputeUniformValues({ 4, { 1.0, 2.0, 3.0, 4.0 } }, out);
	EXPECT_DOUBLE_EQ(4.0, out[3]);
}

// [Doruk-Aksoy] A duration sets when it ends, and what is left never reads 0 while it runs.
TEST(AcsShaders, DurationsAndWhatIsLeft)
{
	EXPECT_EQ(0, ComputeExpiryTic(100, 0));
	EXPECT_EQ(135, ComputeExpiryTic(100, 35));
	EXPECT_EQ(0, ComputeRemainingTics(100, 0));
	EXPECT_EQ(35, ComputeRemainingTics(100, 135));
	EXPECT_EQ(1, ComputeRemainingTics(135, 135));
	EXPECT_EQ(70, ComputeElapsedTics(170, 100));
	EXPECT_EQ(0, ComputeElapsedTics(90, 100));
}

// [Doruk-Aksoy] A joiner gets everyone's entries and their own, not another player's, and a run-out enable only for its uniforms.
TEST(AcsShaders, AJoinerGetsEveryonesAndTheirOwn)
{
	ShaderTable t;
	const double v[4] = { 1, 2, 3, 4 };
	zx::postprocess::ComputeSetShaderEnabled(t, "vig", -1, true, 10, 0);
	zx::postprocess::ComputeSetShaderEnabled(t, "fire", 2, true, 10, 50);
	zx::postprocess::ComputeSetShaderEnabled(t, "fire", 3, true, 10, 0);
	zx::postprocess::ComputeSetShaderEnabled(t, "gone", -1, true, 10, 20);
	zx::postprocess::ComputeSetShaderEnabled(t, "kept", -1, true, 10, 20);
	zx::postprocess::ComputeSetShaderUniform(t, "kept", -1, "x", v);
	std::vector<zx::postprocess::ShaderEntry> j = ComputeJoinEntries(t, 2, 30);
	ASSERT_EQ(3u, j.size());
	EXPECT_EQ("vig", j[0].shader);
	EXPECT_EQ("fire", j[1].shader);
	EXPECT_EQ(2, j[1].player);
	EXPECT_EQ("kept", j[2].shader);
	EXPECT_EQ(-1, j[2].enabled);
	EXPECT_EQ(1u, j[2].uniforms.size());
}

// [Doruk-Aksoy] A leaving player's entries go, bumping the generation only when there were some.
TEST(AcsShaders, ALeavingPlayersEntriesGo)
{
	ShaderTable t;
	zx::postprocess::ComputeSetShaderEnabled(t, "fire", 2, true, 0, 0);
	zx::postprocess::ComputeSetShaderEnabled(t, "vig", -1, true, 0, 0);
	const unsigned g = t.generation;
	ComputeDropPlayer(t, 5);
	EXPECT_EQ(g, t.generation);
	ComputeDropPlayer(t, 2);
	ASSERT_EQ(1u, t.entries.size());
	EXPECT_EQ("vig", t.entries[0].shader);
	EXPECT_EQ(g + 1, t.generation);
}
