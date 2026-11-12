// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Per-actor GLDEFS materials (fua_Material): the level's table of actors whose uniforms a script set, the rows the GPU reads them from, and the fua_ ACS calls that change them.

#ifndef ZX_ACTORMATERIAL_COMPUTE_H
#define ZX_ACTORMATERIAL_COMPUTE_H

#include "features/acs-shaders/computation/acsshaders_compute.h"

#include <array>
#include <string>
#include <vector>

namespace zx { namespace actormaterials {

// [Doruk-Aksoy] The vec4s each material and each actor's own values take, one per uniform, how many materials GLDEFS may declare, and how many actors may hold values of their own at once.
enum { kActorUniformVec4s = 8, kMaxActorMaterials = 256, kMaxActorInstances = 4096 };

// [Doruk-Aksoy] The ACS functions after the shader ones, compiled into mods' bytecode, so they never move.
enum AcsFunction
{
	kSetActorMaterial = 24006,
	kSetActorUniform1f,
	kSetActorUniform2f,
	kSetActorUniform3f,
	kSetActorUniform4f,
	kSetActorUniform1i,
};

// [Doruk-Aksoy] Whether `function` is one of ours.
bool ComputeIsActorMaterialFunction(int function);

// [Doruk-Aksoy] fua_SetActorUniform1f..4f(int tid, str uniform, fixed x[, y[, z[, w]]]) and 1i(int tid, str uniform, int x) read from their arguments.
struct ActorUniformCall
{
	bool ok;
	int tid;
	int uniform;   // [Doruk-Aksoy] the uniform's name, as an ACS string number
	zx::acsshaders::UniformValue value;
};
ActorUniformCall ComputeActorUniformCall(int function, const int *args, int argCount);

// [Doruk-Aksoy] One actor's own values: its material, which uniforms it set (a bit each) and their four numbers.
struct ActorInstance
{
	bool used = false;
	int material = -1;
	unsigned set = 0;
	float values[kActorUniformVec4s][4] = {};
};

// [Doruk-Aksoy] The level's actors with values of their own, by slot, and a generation bumped by every change so the frame's copy follows.
struct InstanceTable
{
	unsigned generation = 0;
	std::vector<ActorInstance> instances;
};

// [Doruk-Aksoy] A free slot made used for `material` (the first unused, else a new one), or -1 past kMaxActorInstances.
int ComputeAcquireInstance(InstanceTable &table, int material);

// [Doruk-Aksoy] A slot given back, its values forgotten; a slot out of range or unused changes nothing.
void ComputeReleaseInstance(InstanceTable &table, int slot);

// [Doruk-Aksoy] Uniform `uniform` of slot `slot` set to the value's numbers (ComputeUniformValues), true when that changed anything.
bool ComputeSetInstanceUniform(InstanceTable &table, int slot, int uniform, const zx::acsshaders::UniformValue &value);

// [Doruk-Aksoy] Every slot let go, as a level or round starts afresh.
void ComputeClearInstances(InstanceTable &table);

// [Doruk-Aksoy] The row an actor's uniforms are read from: its own slot's after every material's defaults, else its material's own.
int ComputeActorUniformRow(int material, int slot, int materialCount);

// [Doruk-Aksoy] One material's uniform defaults, a vec4 each in its declaration order.
typedef std::vector<std::array<float, 4>> MaterialDefaults;

// [Doruk-Aksoy] The rows the GPU reads, kActorUniformVec4s vec4s each: each material's defaults, then each slot's values, the ones it left unset its material's defaults.
void ComputeActorUniformRows(const std::vector<MaterialDefaults> &defaults, const InstanceTable &table, std::vector<float> &out);

// [Doruk-Aksoy] One actor's line in fua_actormaterials: `tid=<tid> <material>`, then each uniform as name=x,y,z,w, its own value where set (own may be null) else its default.
std::string ComputeActorUniformLine(int tid, const std::string &material, const std::vector<std::string> &uniforms, const MaterialDefaults &defaults, const ActorInstance *own);

}} // namespace zx::actormaterials

#endif // ZX_ACTORMATERIAL_COMPUTE_H
