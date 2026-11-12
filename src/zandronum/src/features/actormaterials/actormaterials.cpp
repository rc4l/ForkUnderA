// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actormaterials/actormaterials.h"
#include "features/net-session/netrole.h"
#include "features/postprocess/postprocess.h"   // [Doruk-Aksoy] ForEachDefsPart, the GLDEFS lumps a dedicated server reads

#include "actor.h"
#include "c_dispatch.h"
#include "doomstat.h"
#include "farchive.h"
#include "g_level.h"
#include "p_acs.h"
#include "p_local.h"
#include "sc_man.h"
#include "sv_commands.h"
#include "w_wad.h"

#include <cstddef>
#include <cstring>

namespace zx { namespace actormaterials {

namespace pp = zx::postprocess;

namespace {

struct MaterialDecl
{
	std::string name;
	std::vector<UniformDecl> uniforms;
};

std::vector<MaterialDecl> g_materials;
unsigned g_generation = 0;

// [Doruk-Aksoy] An actor's material index, or -1; AActor keeps it one up so a zeroed default is none.
int MaterialOf(const AActor *actor) { return actor->fua_material - 1; }
int SlotOf(const AActor *actor) { return actor->fua_materialSlot - 1; }

// [Doruk-Aksoy] Gives an actor `material` (-1 none), letting its own values go, false when it already had it.
bool SetMaterial(AActor *actor, int material)
{
	if (MaterialOf(actor) == material) return false;
	ComputeReleaseInstance(level.fua_actorMaterials, SlotOf(actor));
	actor->fua_materialSlot = 0;
	actor->fua_material = material + 1;
	return true;
}

// [Doruk-Aksoy] Sets uniform `uniform` of an actor's own values, taking a slot when it has none, false when nothing changed or no slot is free.
bool SetUniform(AActor *actor, int uniform, const zx::acsshaders::UniformValue &value)
{
	if (SlotOf(actor) < 0)
	{
		const int slot = ComputeAcquireInstance(level.fua_actorMaterials, MaterialOf(actor));
		if (slot < 0) return false;
		actor->fua_materialSlot = slot + 1;
	}
	return ComputeSetInstanceUniform(level.fua_actorMaterials, SlotOf(actor), uniform, value);
}

// [Doruk-Aksoy] A material's name as it travels, empty for none.
const char *NameOf(int material) { return material >= 0 ? g_materials[material].name.c_str() : ""; }

void SendUniform(AActor *actor, int uniform, const zx::acsshaders::UniformValue &value, int player)
{
	double d[4];
	zx::acsshaders::ComputeUniformValues(value, d);
	if (player < 0) SERVERCOMMANDS_FuaSetActorUniform(actor, uniform, value.kind, d);
	else SERVERCOMMANDS_FuaSetActorUniform(actor, uniform, value.kind, d, player, SVCF_ONLYTHISCLIENT);
}

// [Doruk-Aksoy] One rebuilt fua_Material block a dedicated server reads: its fua_Uniform lines.
void ReadMaterialPart(int lump, const pp::DeclarationPart &part)
{
	if (part.material.empty()) return;
	const int material = Declare(part.material.c_str());
	if (material < 0) return;
	FScanner sc;
	sc.OpenMem(Wads.GetLumpFullName(lump), part.text.c_str(), (int)part.text.size());
	sc.MustGetToken('{');
	while (!sc.CheckToken('}'))
	{
		sc.MustGetString();
		ParseUniform(sc, material);
	}
}

} // namespace

void ClearDeclarations()
{
	g_materials.clear();
	g_generation++;
}

int Declare(const char *name)
{
	const int found = MaterialIndex(name);
	if (found >= 0 || MaterialCount() >= kMaxActorMaterials) return found;
	g_materials.push_back({ name, {} });
	g_generation++;
	return (int)g_materials.size() - 1;
}

void ParseUniform(FScanner &sc, int material)
{
	sc.MustGetString();
	const pp::UniformType type = pp::ComputeUniformType(sc.String);
	if (type == pp::kUniformUndefined) sc.ScriptError("Unrecognized uniform type '%s'", sc.String);
	UniformDecl u;
	u.components = pp::ComputeUniformComponents(type);
	u.isInt = type == pp::kUniformInt;
	sc.MustGetString();
	u.name = sc.String;
	if (sc.CheckToken('='))
		for (int i = 0; i < u.components; i++)
		{
			sc.MustGetFloat();
			u.defaults[i] = (float)sc.Float;
		}
	std::vector<UniformDecl> &list = g_materials[material].uniforms;
	for (UniformDecl &e : list)
	{
		if (e.name != u.name) continue;
		if (e.components != u.components || e.isInt != u.isInt) sc.ScriptError("fua_Uniform '%s' of '%s' is declared again as another type", u.name.c_str(), g_materials[material].name.c_str());
		e = u;
		g_generation++;
		return;
	}
	if ((int)list.size() >= kActorUniformVec4s) sc.ScriptError("Too many fua_Uniform lines in fua_Material '%s': %d at most", g_materials[material].name.c_str(), (int)kActorUniformVec4s);
	list.push_back(u);
	g_generation++;
}

int MaterialCount() { return (int)g_materials.size(); }

const char *MaterialName(int material) { return NameOf(material); }

int MaterialIndex(const char *name)
{
	if (name == NULL || *name == 0) return -1;
	for (size_t i = 0; i < g_materials.size(); i++)
		if (stricmp(g_materials[i].name.c_str(), name) == 0) return (int)i;
	return -1;
}

const std::vector<UniformDecl> &Uniforms(int material) { return g_materials[material].uniforms; }

int UniformIndex(int material, const char *uniform)
{
	if (material < 0 || material >= MaterialCount() || uniform == NULL) return -1;
	const std::vector<UniformDecl> &list = g_materials[material].uniforms;
	for (size_t i = 0; i < list.size(); i++)
		if (list[i].name == uniform) return (int)i;
	return -1;
}

std::vector<MaterialDefaults> Defaults()
{
	std::vector<MaterialDefaults> out(g_materials.size());
	for (size_t m = 0; m < g_materials.size(); m++)
		for (const UniformDecl &u : g_materials[m].uniforms)
			out[m].push_back({ { u.defaults[0], u.defaults[1], u.defaults[2], u.defaults[3] } });
	return out;
}

unsigned DeclarationsGeneration() { return g_generation; }

void ReadDeclarations(const char *gameDefsLump)
{
	ClearDeclarations();
	pp::ForEachDefsPart(gameDefsLump, &ReadMaterialPart);
}

void InitActor(AActor *actor)
{
	actor->fua_material = MaterialIndex(actor->fua_MaterialName.GetChars()) + 1;
	actor->fua_materialSlot = 0;
}

void ReleaseActor(AActor *actor)
{
	ComputeReleaseInstance(level.fua_actorMaterials, SlotOf(actor));
	actor->fua_materialSlot = 0;
}

int CallActorMaterialFunction(AActor *activator, int function, const int *args, int argCount)
{
	const char *name = argCount > 1 ? FBehavior::StaticLookupString(args[1]) : NULL;
	if (name == NULL) return 0;
	// [Doruk-Aksoy] A client applies a CLIENTSIDE script's change itself; only a server sends.
	const bool send = zx::net::IsServer();
	const int tid = args[0];
	FActorIterator it(tid);
	int changed = 0;
	if (function == kSetActorMaterial)
	{
		const bool none = *name == 0 || stricmp(name, "none") == 0;
		const int material = none ? -1 : MaterialIndex(name);
		// [Doruk-Aksoy] A name GLDEFS does not declare is refused, as fua_SetShaderEnabled refuses one.
		if (!none && material < 0) return 0;
		for (AActor *a = tid == 0 ? activator : it.Next(); a != NULL; a = tid == 0 ? NULL : it.Next())
		{
			changed++;
			if (SetMaterial(a, material) && send) SERVERCOMMANDS_FuaSetActorMaterial(a, NameOf(material));
		}
		return changed;
	}
	const ActorUniformCall call = ComputeActorUniformCall(function, args, argCount);
	if (!call.ok) return 0;
	for (AActor *a = tid == 0 ? activator : it.Next(); a != NULL; a = tid == 0 ? NULL : it.Next())
	{
		const int uniform = UniformIndex(MaterialOf(a), name);
		if (uniform < 0) continue;
		changed++;
		if (SetUniform(a, uniform, call.value) && send) SendUniform(a, uniform, call.value, -1);
	}
	return changed;
}

void ApplyActorMaterial(AActor *actor, const char *material)
{
	if (actor != NULL) SetMaterial(actor, MaterialIndex(material));
}

void ApplyActorUniform(AActor *actor, int uniform, const zx::acsshaders::UniformValue &value)
{
	const int material = actor != NULL ? MaterialOf(actor) : -1;
	if (material >= 0 && material < MaterialCount() && uniform >= 0 && uniform < (int)Uniforms(material).size()) SetUniform(actor, uniform, value);
}

void SendActorMaterialTo(AActor *actor, int player)
{
	const AActor *defaults = actor->GetDefault();
	if (MaterialOf(actor) != MaterialIndex(defaults->fua_MaterialName.GetChars())) SERVERCOMMANDS_FuaSetActorMaterial(actor, NameOf(MaterialOf(actor)), player, SVCF_ONLYTHISCLIENT);
	const int slot = SlotOf(actor);
	if (slot < 0 || slot >= (int)level.fua_actorMaterials.instances.size()) return;
	const ActorInstance &own = level.fua_actorMaterials.instances[slot];
	for (int u = 0; u < kActorUniformVec4s; u++)
		if ((own.set & (1u << u)) != 0)
		{
			const zx::acsshaders::UniformValue value = { 4, { own.values[u][0], own.values[u][1], own.values[u][2], own.values[u][3] } };
			SendUniform(actor, u, value, player);
		}
}

void SerializeActor(FArchive &arc, AActor *actor)
{
	// [Doruk-Aksoy] An older save starts its actors on their DECORATE material.
	if (SaveVersion < 4527)
	{
		if (arc.IsLoading()) InitActor(actor);
		return;
	}
	FString name = NameOf(MaterialOf(actor));
	DWORD set = 0;
	const int slot = SlotOf(actor);
	if (arc.IsStoring() && slot >= 0 && slot < (int)level.fua_actorMaterials.instances.size()) set = level.fua_actorMaterials.instances[slot].set;
	arc << name << set;
	float values[kActorUniformVec4s][4] = {};
	if (arc.IsStoring() && set != 0) memcpy(values, level.fua_actorMaterials.instances[slot].values, sizeof(values));
	for (int u = 0; u < kActorUniformVec4s; u++)
		if ((set & (1u << u)) != 0) arc << values[u][0] << values[u][1] << values[u][2] << values[u][3];
	if (arc.IsStoring()) return;
	actor->fua_material = MaterialIndex(name.GetChars()) + 1;
	actor->fua_materialSlot = 0;
	for (int u = 0; u < kActorUniformVec4s; u++)
		if ((set & (1u << u)) != 0) SetUniform(actor, u, zx::acsshaders::UniformValue{ 4, { values[u][0], values[u][1], values[u][2], values[u][3] } });
}

void ResetLevelInstances() { ComputeClearInstances(level.fua_actorMaterials); }

void ResetRound()
{
	ResetLevelInstances();
	TThinkerIterator<AActor> it;
	for (AActor *a; (a = it.Next()) != NULL;) InitActor(a);
}

}} // namespace zx::actormaterials

// [Doruk-Aksoy] Every actor with a tid and a material, its material and each uniform's values as it draws them (ComputeActorUniformLine), for netgame tests.
DEV_MODE_CCMD(fua_actormaterials)
{
	using namespace zx::actormaterials;
	const std::vector<MaterialDefaults> defaults = Defaults();
	TThinkerIterator<AActor> it;
	for (AActor *a; (a = it.Next()) != NULL;)
	{
		const int material = a->fua_material - 1, slot = a->fua_materialSlot - 1;
		if (a->tid == 0 || material < 0) continue;
		const ActorInstance *own = slot >= 0 && slot < (int)level.fua_actorMaterials.instances.size() ? &level.fua_actorMaterials.instances[slot] : NULL;
		std::vector<std::string> names;
		for (const UniformDecl &u : Uniforms(material)) names.push_back(u.name);
		Printf("fua_actormaterials: %s\n", ComputeActorUniformLine(a->tid, MaterialName(material), names, defaults[material], own).c_str());
	}
}

// [Doruk-Aksoy] Reads GLDEFS's fua_Material declarations again as a dedicated server's null renderer does, so a netgame test checks that path on any build.
DEV_MODE_CCMD(fua_actormaterials_reread)
{
	zx::actormaterials::ReadDeclarations(NULL);
	Printf("fua_actormaterials_reread: %d declared\n", zx::actormaterials::MaterialCount());
}
