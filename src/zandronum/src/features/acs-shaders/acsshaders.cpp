// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-shaders/acsshaders.h"
#include "features/net-session/netrole.h"
#include "features/postprocess/postprocess.h"   // [Doruk-Aksoy] PostShaderDeclared

#include "actor.h"
#include "d_player.h"
#include "doomstat.h"
#include "farchive.h"
#include "g_level.h"
#include "p_acs.h"
#include "sv_commands.h"

namespace zx { namespace acsshaders {

namespace pp = zx::postprocess;

namespace {

Role RoleNow()
{
	if (zx::net::IsServer()) return kServer;
	return zx::net::InClientMode() ? kClient : kOffline;
}

int ActivatorPlayer(AActor *activator)
{
	if (activator == NULL) return kActivatorNone;
	return activator->player != NULL ? int(activator->player - players) : kActivatorNotPlayer;
}

// [Doruk-Aksoy] An entry's on or off as it stands at `now`: the tics it has left and, when on, how long it has run.
void SendEnabled(int sendTo, const pp::ShaderEntry &e, int now)
{
	const bool own = e.player != -1, on = e.enabled == 1;
	const int remaining = ComputeRemainingTics(now, e.expiresTic), elapsed = on ? ComputeElapsedTics(now, e.enabledTic) : 0;
	if (sendTo == kSendAll) SERVERCOMMANDS_FuaSetShaderEnabled(e.shader.c_str(), own, on, remaining, elapsed);
	else if (sendTo >= 0) SERVERCOMMANDS_FuaSetShaderEnabled(e.shader.c_str(), own, on, remaining, elapsed, sendTo, SVCF_ONLYTHISCLIENT);
}

void SendUniform(int sendTo, const char *shader, const char *uniform, bool own, const UniformValue &value)
{
	if (sendTo == kSendAll) SERVERCOMMANDS_FuaSetShaderUniform(shader, uniform, own, value.kind, value.values);
	else if (sendTo >= 0) SERVERCOMMANDS_FuaSetShaderUniform(shader, uniform, own, value.kind, value.values, sendTo, SVCF_ONLYTHISCLIENT);
}

// [Doruk-Aksoy] False when the uniform already stood at this value, so nothing is sent.
bool SetUniform(int player, const char *shader, const char *uniform, const UniformValue &value)
{
	double values[4];
	ComputeUniformValues(value, values);
	return pp::ComputeSetShaderUniform(level.fua_postShaders, shader, player, uniform, values, value.kind);
}

} // namespace

int CallShaderFunction(AActor *activator, int function, const int *args, int argCount)
{
	const char *shader = argCount > 0 ? FBehavior::StaticLookupString(args[0]) : NULL;
	if (shader == NULL) return 0;
	const int now = level.maptime;
	if (function == kSetShaderEnabled)
	{
		const EnableCall call = ComputeEnableCall(args, argCount);
		const Route route = ComputeShaderRoute(RoleNow(), call.activatorOnly, ActivatorPlayer(activator), consoleplayer);
		// [Doruk-Aksoy] A name GLDEFS does not declare is ignored, so no script fills the level's state with them.
		if (!call.ok || !route.apply || !pp::PostShaderDeclared(shader, "")) return 0;
		if (pp::ComputeSetShaderEnabled(level.fua_postShaders, shader, route.player, call.enable, now, ComputeExpiryTic(now, call.durationTics)))
			SendEnabled(route.sendTo, *pp::ComputeShaderEntry(level.fua_postShaders, shader, route.player), now);
		return 1;
	}
	const UniformCall call = ComputeUniformCall(function, args, argCount);
	const char *uniform = argCount > 1 ? FBehavior::StaticLookupString(args[1]) : NULL;
	const Route route = ComputeShaderRoute(RoleNow(), call.activatorOnly, ActivatorPlayer(activator), consoleplayer);
	if (!call.ok || uniform == NULL || !route.apply || !pp::PostShaderDeclared(shader, uniform)) return 0;
	if (SetUniform(route.player, shader, uniform, call.value)) SendUniform(route.sendTo, shader, uniform, route.player != -1, call.value);
	return 1;
}

void ApplyShaderEnabled(const char *shader, bool own, bool enabled, int remainingTics, int elapsedTics)
{
	if (!pp::PostShaderDeclared(shader, "")) return;
	const int now = level.maptime;
	pp::ComputeSetShaderEnabled(level.fua_postShaders, shader, own ? consoleplayer : -1, enabled, now, ComputeExpiryTic(now, remainingTics), now - elapsedTics);
}

void ApplyShaderUniform(const char *shader, const char *uniform, bool own, const UniformValue &value)
{
	if (pp::PostShaderDeclared(shader, uniform)) SetUniform(own ? consoleplayer : -1, shader, uniform, value);
}

void SendShaderStateTo(int player)
{
	const int now = level.maptime;
	for (const pp::ShaderEntry &e : ComputeJoinEntries(level.fua_postShaders, player, now))
	{
		if (e.enabled != -1) SendEnabled(player, e, now);
		for (const pp::ShaderEntry::Uniform &u : e.uniforms)
		{
			UniformValue value = { u.kind, { u.values[0], u.values[1], u.values[2], u.values[3] } };
			SendUniform(player, e.shader.c_str(), u.name.c_str(), e.player != -1, value);
		}
	}
}

void DropPlayerShaders(int player) { ComputeDropPlayer(level.fua_postShaders, player); }

void ResetLevelShaders() { pp::ComputeClearShaderTable(level.fua_postShaders); }

void SerializeLevelShaders(FArchive &arc)
{
	if (SaveVersion < 4526) return;
	pp::ShaderTable &table = level.fua_postShaders;
	DWORD count = (DWORD)table.entries.size();
	arc << count;
	if (arc.IsLoading()) table.entries.assign(count, pp::ShaderEntry());
	for (pp::ShaderEntry &e : table.entries)
	{
		FString shader = e.shader.c_str();
		SDWORD player = e.player, enabled = e.enabled, since = e.enabledTic, expires = e.expiresTic;
		DWORD uniforms = (DWORD)e.uniforms.size();
		arc << shader << player << enabled << since << expires << uniforms;
		if (arc.IsLoading())
		{
			e.shader = shader.GetChars();
			e.player = player;
			e.enabled = enabled;
			e.enabledTic = since;
			e.expiresTic = expires;
			e.uniforms.assign(uniforms, pp::ShaderEntry::Uniform());
		}
		for (pp::ShaderEntry::Uniform &u : e.uniforms)
		{
			FString name = u.name.c_str();
			SDWORD kind = u.kind;
			arc << name << kind << u.values[0] << u.values[1] << u.values[2] << u.values[3];
			u.name = name.GetChars();
			u.kind = kind;
		}
	}
	if (arc.IsLoading()) table.generation++;
}

}} // namespace zx::acsshaders
