// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The fua_ ACS functions that do what UZDoom's ZScript PPShader does to GLDEFS post-process shaders: their numbers and arguments, who a call reaches on this machine, and what a joining player is sent.

#ifndef ZX_ACSSHADERS_COMPUTE_H
#define ZX_ACSSHADERS_COMPUTE_H

#include "features/postprocess/computation/postprocess_compute.h"

#include <vector>

namespace zx { namespace acsshaders {

// [Doruk-Aksoy] The functions' numbers, a block no upstream reaches (GZDoom's run to the 400s, Zandronum's from 100, ZDaemon's at 19620); compiled into mods' bytecode, they never move.
enum AcsFunction
{
	kSetShaderEnabled = 24000,
	kSetShaderUniform1f,
	kSetShaderUniform2f,
	kSetShaderUniform3f,
	kSetShaderUniform4f,
	kSetShaderUniform1i,
};

// [Doruk-Aksoy] Whether `function` is one of ours.
bool ComputeIsShaderFunction(int function);

// [Doruk-Aksoy] Where a call runs: offline, on a server, or on a client (a CLIENTSIDE script).
enum Role { kOffline, kServer, kClient };

// [Doruk-Aksoy] Who a call is sent to: nobody, every client, or (any value from 0 up) that player's.
const int kSendNone = -2;
const int kSendAll = -1;

// [Doruk-Aksoy] An activator that is not a player (a monster), and none at all (the world).
const int kActivatorNotPlayer = -1;
const int kActivatorNone = -2;

// [Doruk-Aksoy] What one call does on this machine: whether it changes the level's state and whose entry (-1 everyone's), and who it is sent to.
struct Route
{
	bool apply;
	int player;
	int sendTo;
};

// [Doruk-Aksoy] The route of a call made by `activatorPlayer` (a player number, kActivatorNotPlayer or kActivatorNone), for everyone or only its activator; offline and on a client no activator means the console player, as the local player runs the script.
Route ComputeShaderRoute(Role role, bool activatorOnly, int activatorPlayer, int consolePlayer);

// [Doruk-Aksoy] fua_SetShaderEnabled(str shader, bool enable [, bool activatorOnly [, int durationTics]]) read from its arguments.
struct EnableCall
{
	bool ok;
	bool enable;
	bool activatorOnly;
	int durationTics;
};
EnableCall ComputeEnableCall(const int *args, int argCount);

// [Doruk-Aksoy] A uniform's value as it travels: kind 0 one int, 1 to 4 that many floats.
struct UniformValue
{
	int kind;
	double values[4];
};

// [Doruk-Aksoy] fua_SetShaderUniform1f..4f(str shader, str uniform, fixed x[, y[, z[, w]]] [, bool activatorOnly]) and 1i(str, str, int [, bool]) read from their arguments, the floats from ACS's 16.16 fixed point.
struct UniformCall
{
	bool ok;
	UniformValue value;
	bool activatorOnly;
};
UniformCall ComputeUniformCall(int function, const int *args, int argCount);

// [Doruk-Aksoy] The four numbers a uniform's value sets, the rest 0 but w 1 as UZDoom's console sets them.
void ComputeUniformValues(const UniformValue &value, double out[4]);

// [Doruk-Aksoy] The tic a shader turned on at `now` for `durationTics` goes off at, 0 for never.
int ComputeExpiryTic(int now, int durationTics);

// [Doruk-Aksoy] The tics a timed shader has left at `now` (at least 1 while it runs), 0 for one that never times out.
int ComputeRemainingTics(int now, int expiresTic);

// [Doruk-Aksoy] The tics a shader has run at `now` since the tic it went on, sent so a client's clock agrees with the server's.
int ComputeElapsedTics(int now, int sinceTic);

// [Doruk-Aksoy] The level's entries a player joining at `now` is sent: everyone's and their own, an enable that already ran out sending only its uniforms.
std::vector<zx::postprocess::ShaderEntry> ComputeJoinEntries(const zx::postprocess::ShaderTable &table, int player, int now);

// [Doruk-Aksoy] Drops a leaving player's own entries, so whoever takes the slot starts from everyone's.
void ComputeDropPlayer(zx::postprocess::ShaderTable &table, int player);

}} // namespace zx::acsshaders

#endif // ZX_ACSSHADERS_COMPUTE_H
