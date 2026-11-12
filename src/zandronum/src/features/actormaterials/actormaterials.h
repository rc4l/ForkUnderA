// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Per-actor GLDEFS materials: the fua_Material declarations, each actor's material and own uniform values, the fua_ ACS calls that change them, what a client is sent, and what a savegame keeps.

#ifndef ZX_ACTORMATERIALS_H
#define ZX_ACTORMATERIALS_H

#include "features/actormaterials/computation/actormaterial_compute.h"

#include <string>
#include <vector>

class AActor;
class FArchive;
class FScanner;

namespace zx { namespace actormaterials {

// [Doruk-Aksoy] One fua_Uniform of a material: its name, its components (1 to 4), whether it is an int, and its default.
struct UniformDecl
{
	std::string name;
	int components = 1;
	bool isInt = false;
	float defaults[4] = { 0.f, 0.f, 0.f, 1.f };
};

// [Doruk-Aksoy] Drops every declaration, as GLDEFS is read again.
void ClearDeclarations();

// [Doruk-Aksoy] The index of material `name` (any case), declared now when new, or -1 past kMaxActorMaterials.
int Declare(const char *name);

// [Doruk-Aksoy] One `fua_Uniform <type> <name> [= <values>]` of material `material`, the scanner past fua_Uniform; a material takes kActorUniformVec4s at most.
void ParseUniform(FScanner &sc, int material);

int MaterialCount();
const char *MaterialName(int material);

// [Doruk-Aksoy] Material `name`'s index (any case), or -1 for none or an empty name.
int MaterialIndex(const char *name);

// [Doruk-Aksoy] Material `material`'s uniforms in declaration order, and one's index by name, or -1.
const std::vector<UniformDecl> &Uniforms(int material);
int UniformIndex(int material, const char *uniform);

// [Doruk-Aksoy] Every material's uniform defaults, for the rows the GPU reads.
std::vector<MaterialDefaults> Defaults();

// [Doruk-Aksoy] Bumped whenever the declarations change, so a renderer makes its programs again.
unsigned DeclarationsGeneration();

// [Doruk-Aksoy] Reads only the fua_Material blocks' names and uniforms from the game's *DEFS lump (NULL for none) and every GLDEFS, for a dedicated server whose null renderer reads none of the rest.
void ReadDeclarations(const char *gameDefsLump);

// [Doruk-Aksoy] A spawned actor's material from its class's DECORATE fua_Material.
void InitActor(AActor *actor);

// [Doruk-Aksoy] A destroyed actor's own values given back.
void ReleaseActor(AActor *actor);

// [Doruk-Aksoy] Runs fua_SetActorMaterial or a fua_SetActorUniform for a script whose activator is `activator`, returning how many actors it changed or set.
int CallActorMaterialFunction(AActor *activator, int function, const int *args, int argCount);

// [Doruk-Aksoy] A server's change arriving on a client: an actor's material by name (empty for none), or one of its uniforms by index.
void ApplyActorMaterial(AActor *actor, const char *material);
void ApplyActorUniform(AActor *actor, int uniform, const zx::acsshaders::UniformValue &value);

// [Doruk-Aksoy] Sends a joining player an actor's material, when DECORATE does not already give it, and its own values.
void SendActorMaterialTo(AActor *actor, int player);

// [Doruk-Aksoy] An actor's material and own values in a savegame, from SAVEVER 4527 on.
void SerializeActor(FArchive &arc, AActor *actor);

// [Doruk-Aksoy] Every actor's own values let go, as a level starts afresh.
void ResetLevelInstances();

// [Doruk-Aksoy] Every actor back on its DECORATE material with no values of its own, as a round reset starts the level afresh on the server and each client.
void ResetRound();

}} // namespace zx::actormaterials

#endif // ZX_ACTORMATERIALS_H
