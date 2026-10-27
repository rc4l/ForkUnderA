// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-shaders/computation/acsshaders_compute.h"

#include <algorithm>

namespace zx { namespace acsshaders {

bool ComputeIsShaderFunction(int function) { return function >= kSetShaderEnabled && function <= kSetShaderUniform1i; }

Route ComputeShaderRoute(Role role, bool activatorOnly, int activatorPlayer, int consolePlayer)
{
	Route r = { false, -1, kSendNone };
	if (!activatorOnly)
	{
		r.apply = true;
		r.sendTo = role == kServer ? kSendAll : kSendNone;
		return r;
	}
	if (role == kServer)
	{
		// [Doruk-Aksoy] A monster or the world has no client to send it to.
		if (activatorPlayer < 0) return r;
		r.apply = true;
		r.player = activatorPlayer;
		r.sendTo = activatorPlayer;
		return r;
	}
	const int who = activatorPlayer == kActivatorNone ? consolePlayer : activatorPlayer;
	if (who < 0 || who != consolePlayer) return r;
	r.apply = true;
	r.player = consolePlayer;
	return r;
}

EnableCall ComputeEnableCall(const int *args, int argCount)
{
	EnableCall c = { argCount >= 2, false, false, 0 };
	if (!c.ok) return c;
	c.enable = args[1] != 0;
	c.activatorOnly = argCount > 2 && args[2] != 0;
	c.durationTics = argCount > 3 ? std::max(0, args[3]) : 0;
	return c;
}

UniformCall ComputeUniformCall(int function, const int *args, int argCount)
{
	UniformCall c = { false, { 0, { 0.0, 0.0, 0.0, 0.0 } }, false };
	if (function < kSetShaderUniform1f || function > kSetShaderUniform1i) return c;
	c.value.kind = function == kSetShaderUniform1i ? 0 : function - kSetShaderUniform1f + 1;
	const int count = c.value.kind == 0 ? 1 : c.value.kind;
	if (argCount < 2 + count) return c;
	for (int i = 0; i < count; i++) c.value.values[i] = c.value.kind == 0 ? (double)args[2] : args[2 + i] / 65536.0;
	c.activatorOnly = argCount > 2 + count && args[2 + count] != 0;
	c.ok = true;
	return c;
}

void ComputeUniformValues(const UniformValue &value, double out[4])
{
	out[0] = out[1] = out[2] = 0.0;
	out[3] = 1.0;
	const int count = value.kind == 0 ? 1 : std::min(value.kind, 4);
	for (int i = 0; i < count; i++) out[i] = value.values[i];
}

int ComputeExpiryTic(int now, int durationTics) { return durationTics > 0 ? now + durationTics : 0; }

int ComputeRemainingTics(int now, int expiresTic) { return expiresTic > 0 ? std::max(1, expiresTic - now) : 0; }

int ComputeElapsedTics(int now, int sinceTic) { return std::max(0, now - sinceTic); }

std::vector<zx::postprocess::ShaderEntry> ComputeJoinEntries(const zx::postprocess::ShaderTable &table, int player, int now)
{
	std::vector<zx::postprocess::ShaderEntry> out;
	for (const zx::postprocess::ShaderEntry &e : table.entries)
	{
		if (e.player != -1 && e.player != player) continue;
		zx::postprocess::ShaderEntry copy = e;
		if (copy.enabled == 1 && copy.expiresTic > 0 && now >= copy.expiresTic) copy.enabled = -1;
		if (copy.enabled == -1 && copy.uniforms.empty()) continue;
		out.push_back(copy);
	}
	return out;
}

void ComputeDropPlayer(zx::postprocess::ShaderTable &table, int player)
{
	const size_t before = table.entries.size();
	table.entries.erase(std::remove_if(table.entries.begin(), table.entries.end(),
		[player](const zx::postprocess::ShaderEntry &e) { return e.player == player; }), table.entries.end());
	if (table.entries.size() != before) table.generation++;
}

}} // namespace zx::acsshaders
