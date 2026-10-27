// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/postprocess/postprocess.h"
#include "features/gamecopy/level.h"
#include "features/handoff/effects.h"   // [Doruk-Aksoy] QueuedBesideTic
#include "features/hwrender/backendonly.h"
#include "features/gldefs/gldefs.h"   // [Doruk-Aksoy] gl_GameDefsLump

#include "doomtype.h"
#include "doomstat.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "g_level.h"
#include "sc_man.h"
#include "w_wad.h"

#include <cstring>
#include <memory>

// [Doruk-Aksoy] Ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad: off skips every custom post-process shader.
CVAR(Bool, gl_custompost, true, 0)

namespace zx { namespace postprocess {

namespace {

std::vector<ShaderDecl> g_shaders;
unsigned g_generation = 0;

CvarKind KindOf(const FBaseCVar *cvar)
{
	switch (cvar->GetRealType())
	{
	case CVAR_Int: return kCvarInt;
	case CVAR_Float: return kCvarFloat;
	case CVAR_Color: return kCvarColor;
	default: return kCvarOther;
	}
}

// [Doruk-Aksoy] A cvar's value as the four numbers its uniform takes.
void CvarValues(FBaseCVar *cvar, double out[4])
{
	out[0] = out[1] = out[2] = 0.0;
	out[3] = 1.0;
	switch (KindOf(cvar))
	{
	case kCvarInt: out[0] = cvar->GetGenericRep(CVAR_Int).Int; break;
	case kCvarFloat: out[0] = cvar->GetGenericRep(CVAR_Float).Float; break;
	case kCvarColor: ComputeColorUniform((unsigned)cvar->GetGenericRep(CVAR_Int).Int, out); break;
	default: break;
	}
}

// [Doruk-Aksoy] The values a uniform starts with, `= cvar <name>` binding it to that cvar instead.
void ParseUniformValues(FScanner &sc, UniformType type, const std::string &shaderName, double values[4], FBaseCVar *&cvar)
{
	if (!sc.CheckToken('=')) return;
	if (sc.CheckString("cvar"))
	{
		if (shaderName.empty()) sc.ScriptError("Shader must have a name to use cvar uniforms");
		sc.MustGetString();
		FBaseCVar *found = FindCVar(sc.String, NULL);
		if (found == NULL)
		{
			sc.ScriptMessage("Unknown cvar passed to cvar_uniform");
			return;
		}
		if (!ComputeCvarUniformFits(KindOf(found), type))
		{
			sc.ScriptError("CVar '%s' type is not convertible to this uniform's type", sc.String);
			return;
		}
		cvar = found;
		CvarValues(found, values);
		return;
	}
	for (int i = 0; i < ComputeUniformComponents(type); i++)
	{
		if (type == kUniformInt) { sc.MustGetNumber(); values[i] = sc.Number; }
		else { sc.MustGetFloat(); values[i] = sc.Float; }
	}
}

// [Doruk-Aksoy] Adds or replaces a uniform, as UZDoom's map keeps a name's last declaration.
void AddUniform(ShaderDecl &d, const std::string &name, UniformType type, const double values[4], FBaseCVar *cvar)
{
	std::array<double, 4> v = { { values[0], values[1], values[2], values[3] } };
	for (size_t i = 0; i < d.uniforms.size(); i++)
		if (d.uniforms[i].name == name)
		{
			d.uniforms[i].type = type;
			d.defaults[i] = v;
			d.cvars[i] = cvar;
			return;
		}
	d.uniforms.push_back({ name, type, 0 });
	d.defaults.push_back(v);
	d.cvars.push_back(cvar);
}

void AddTexture(ShaderDecl &d, const std::string &name, const std::string &file)
{
	for (size_t i = 0; i < d.textureNames.size(); i++)
		if (d.textureNames[i] == name) { d.textureFiles[i] = file; return; }
	d.textureNames.push_back(name);
	d.textureFiles.push_back(file);
}

int IndexOf(const char *name)
{
	for (size_t i = 0; i < g_shaders.size(); i++)
		if (g_shaders[i].name == name) return (int)i;
	return -1;
}

// [Doruk-Aksoy] What the console asked of a shader, carried to the game's own time to change the level.
struct ConsoleChange
{
	std::string shader, uniform;
	int value = 0;
	bool setUniform = false;
	double values[4] = { 0, 0, 0, 1 };
};

void ApplyConsoleChange(intptr_t change, intptr_t)
{
	std::unique_ptr<ConsoleChange> c((ConsoleChange *)change);
	bool found = false;
	for (const ShaderDecl &d : g_shaders)
	{
		if (d.name != c->shader) continue;
		found = true;
		if (c->setUniform) { ComputeSetShaderUniform(level.fua_postShaders, d.name, -1, c->uniform, c->values); continue; }
		const bool now = ComputeEffectiveEnabled(level.fua_postShaders, d.name, -1, d.enabled, level.maptime).enabled;
		ComputeSetShaderEnabled(level.fua_postShaders, d.name, -1, ComputeConsoleEnable(c->value, now), level.maptime, 0);
	}
	if (!found) Printf("No shader named '%s' found\n", c->shader.c_str());
	else if (c->setUniform) Printf("Changed uniforms of %s named %s\n", c->shader.c_str(), c->uniform.c_str());
	else if (c->value != -1) Printf("Changed active state of all instances of %s to %s\n", c->shader.c_str(), c->value ? "On" : "Off");
	else Printf("Toggled active state of all instances of %s\n", c->shader.c_str());
}

// [Doruk-Aksoy] The level's state changes between tics, so beside a tic on the game thread the console's change waits for the handoff.
void ChangeLevelShaders(ConsoleChange *change)
{
	const zx::handoff::UIEffect effect = { &ApplyConsoleChange, (intptr_t)change, 0 };
	if (!zx::handoff::QueuedBesideTic(effect)) effect.run(effect.a, effect.b);
}

// [Doruk-Aksoy] A uniform's values as the console's listing shows them, from the render copy since a tic may be running: the level's, else its cvar's or default.
void CurrentValues(const ShaderDecl &d, size_t i, double out[4])
{
	if (const double *set = ComputeEffectiveUniform(zx::gamecopy::LevelShaders(), d.name, consoleplayer, d.uniforms[i].name))
	{
		memcpy(out, set, sizeof(double) * 4);
		return;
	}
	if (d.cvars[i] != NULL) { CvarValues(d.cvars[i], out); return; }
	for (int k = 0; k < 4; k++) out[k] = d.defaults[i][k];
}

} // namespace

void ClearPostProcessShaders()
{
	g_shaders.clear();
	g_generation++;
}

// [Doruk-Aksoy] Ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad (src/r_data/gldefs.cpp ParseHardwareShader): a post-process block's Name, Shader and version, Uniforms with their defaults or cvars, Textures and Enabled.
void ParsePostProcessShader(FScanner &sc)
{
	sc.MustGetString();
	ShaderDecl d;
	d.stage = ComputeStage(sc.String);
	if (d.stage < 0) sc.ScriptError("Invalid target '%s' for postprocess shader", sc.String);

	sc.MustGetToken('{');
	while (!sc.CheckToken('}'))
	{
		sc.MustGetString();
		if (sc.Compare("shader"))
		{
			sc.MustGetString();
			d.lump = sc.String;
			sc.MustGetNumber();
			if (!ComputeShaderVersionValid(sc.Number)) sc.ScriptError("Shader version must be in range 330 to 450!");
		}
		else if (sc.Compare("name"))
		{
			sc.MustGetString();
			d.name = sc.String;
		}
		else if (sc.Compare("uniform"))
		{
			sc.MustGetString();
			const UniformType type = ComputeUniformType(sc.String);
			if (type == kUniformUndefined) sc.ScriptError("Unrecognized uniform type '%s'", sc.String);
			sc.MustGetString();
			const std::string name = sc.String;
			double values[4] = { 0.0, 0.0, 0.0, 1.0 };
			FBaseCVar *cvar = NULL;
			ParseUniformValues(sc, type, d.name, values, cvar);
			AddUniform(d, name, type, values, cvar);
		}
		else if (sc.Compare("texture"))
		{
			sc.MustGetString();
			const std::string name = sc.String;
			sc.MustGetString();
			AddTexture(d, name, sc.String);
		}
		else if (sc.Compare("enabled"))
		{
			d.enabled = true;
		}
		else
		{
			sc.ScriptError("Unknown keyword '%s'", sc.String);
		}
	}
	d.blockSize = ComputeUniformLayout(d.uniforms);
	g_shaders.push_back(d);
	g_generation++;
}

void NotePostProcessRenderer()
{
	if (!g_shaders.empty() && !zx::hwrender::BackendOnly())
		Printf("%d post-process shaders draw only on the Vulkan renderer\n", (int)g_shaders.size());
}

namespace {

// [Doruk-Aksoy] One GLDEFS lump's post-process blocks and those of the lumps it includes, `depth` stopping an include loop.
void ReadDeclarationsFrom(int lump, int depth)
{
	if (depth > 16) return;
	FMemLump data = Wads.ReadLump(lump);
	const std::string text((const char *)data.GetMem(), data.GetSize());
	for (const DeclarationPart &part : ComputePostProcessDeclarations(text))
	{
		if (part.include)
		{
			const int included = Wads.CheckNumForFullName(part.text.c_str(), true);
			if (included >= 0) ReadDeclarationsFrom(included, depth + 1);
			continue;
		}
		FScanner sc;
		sc.OpenMem(Wads.GetLumpFullName(lump), part.text.c_str(), (int)part.text.size());
		ParsePostProcessShader(sc);
	}
}

void ReadDeclarationsNamed(const char *name)
{
	int lump, last = 0;
	while ((lump = Wads.FindLump(name, &last)) != -1) ReadDeclarationsFrom(lump, 0);
}

void RereadDeclarations(intptr_t, intptr_t)
{
	ReadPostProcessDeclarations();
	Printf("fua_postshaders_reread: %d declared\n", PostShaderCount());
}

} // namespace

void ReadPostProcessDeclarations()
{
	ClearPostProcessShaders();
	if (const char *game = gl_GameDefsLump()) ReadDeclarationsNamed(game);
	ReadDeclarationsNamed("GLDEFS");
}

bool PostShaderDeclared(const char *shader, const char *uniform)
{
	static std::vector<DeclaredShader> declared;
	static unsigned madeFor = ~0u;
	if (madeFor != g_generation)
	{
		declared.clear();
		for (const ShaderDecl &d : g_shaders)
		{
			DeclaredShader s = { d.name, {} };
			for (const UniformField &u : d.uniforms) s.uniforms.push_back(u.name);
			declared.push_back(s);
		}
		madeFor = g_generation;
	}
	return ComputeShaderCallDeclared(declared, shader, uniform);
}

int PostShaderCount() { return (int)g_shaders.size(); }
const ShaderDecl &PostShader(int i) { return g_shaders[i]; }
unsigned PostShadersGeneration() { return g_generation; }
bool PostProcessAllowed() { return gl_custompost; }

EffectiveEnabled PostShaderEnabledNow(int i)
{
	const ShaderDecl &d = g_shaders[i];
	return ComputeEffectiveEnabled(zx::gamecopy::LevelShaders(), d.name, consoleplayer, d.enabled, zx::gamecopy::Level().maptime);
}

void PostShaderBlock(int i, unsigned char *block)
{
	const ShaderDecl &d = g_shaders[i];
	memset(block, 0, d.blockSize);
	for (size_t u = 0; u < d.uniforms.size(); u++)
	{
		double values[4];
		if (const double *set = ComputeEffectiveUniform(zx::gamecopy::LevelShaders(), d.name, consoleplayer, d.uniforms[u].name))
			memcpy(values, set, sizeof(values));
		else if (d.cvars[u] != NULL) CvarValues(d.cvars[u], values);
		else for (int k = 0; k < 4; k++) values[k] = d.defaults[u][k];
		ComputeWriteUniform(block, d.uniforms[u], values);
	}
}

}} // namespace zx::postprocess

using namespace zx::postprocess;

// [Doruk-Aksoy] Ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad (hw_postprocessshader_ccmds.cpp): the console's post-process commands, which change the level as a script would.
CCMD(shaderenable)
{
	if (argv.argc() < 3)
	{
		Printf("Usage: shaderenable [name] [1/0/-1]\nState '-1' toggles the active shader state\n");
		return;
	}
	ConsoleChange *c = new ConsoleChange;
	c->shader = argv[1];
	c->value = atoi(argv[2]);
	ChangeLevelShaders(c);
}

CCMD(shaderuniform)
{
	if (argv.argc() < 3)
	{
		Printf("Usage: shaderuniform [shader name] [uniform name] [[value1 ..]]\n");
		return;
	}
	const int i = IndexOf(argv[1]);
	if (i < 0)
	{
		Printf("No shader named '%s' found\n", argv[1]);
		return;
	}
	if (argv.argc() <= 3)
	{
		const ShaderDecl &d = PostShader(i);
		for (size_t u = 0; u < d.uniforms.size(); u++)
		{
			if (d.uniforms[u].name != argv[2]) continue;
			double v[4];
			CurrentValues(d, u, v);
			Printf("Shader '%s' uniform '%s': %f %f %f\n", argv[1], argv[2], v[0], v[1], v[2]);
		}
		return;
	}
	ConsoleChange *c = new ConsoleChange;
	c->shader = argv[1];
	c->uniform = argv[2];
	c->setUniform = true;
	for (int k = 0; k < 3; k++) c->values[k] = argv.argc() > 3 + k ? atof(argv[3 + k]) : 0.0;
	ChangeLevelShaders(c);
}

// [Doruk-Aksoy] Each shader as this player sees it now, from the render copy: on or off and since when, and every uniform's value; for tests reading a client's state.
CCMD(fua_postshaders)
{
	Printf("fua_postshaders: %d entries at %d\n", (int)zx::gamecopy::LevelShaders().entries.size(), zx::gamecopy::Level().maptime);
	for (int i = 0; i < PostShaderCount(); i++)
	{
		const ShaderDecl &d = PostShader(i);
		std::vector<UniformState> uniforms(d.uniforms.size());
		for (size_t u = 0; u < d.uniforms.size(); u++)
		{
			uniforms[u].name = d.uniforms[u].name;
			CurrentValues(d, u, uniforms[u].values);
		}
		Printf("fua_postshaders: %s\n", ComputeShaderStateLine(d.name, PostShaderEnabledNow(i), uniforms).c_str());
	}
}

// [Doruk-Aksoy] Reads GLDEFS's post-process declarations again as a dedicated server's null renderer does, so a netgame test checks that path on any build.
CCMD(fua_postshaders_reread)
{
	const zx::handoff::UIEffect effect = { &RereadDeclarations, 0, 0 };
	if (!zx::handoff::QueuedBesideTic(effect)) effect.run(effect.a, effect.b);
}

CCMD(listshaders)
{
	for (int i = 0; i < PostShaderCount(); i++) Printf("Shader (%i): %s\n", i, PostShader(i).name.c_str());
}

CCMD(listuniforms)
{
	if (argv.argc() < 2)
	{
		Printf("Usage: listuniforms [name]\n");
		return;
	}
	bool found = false;
	for (int i = 0; i < PostShaderCount(); i++)
	{
		const ShaderDecl &d = PostShader(i);
		if (d.name != argv[1]) continue;
		found = true;
		Printf("Shader '%s' uniforms:\n", argv[1]);
		for (size_t u = 0; u < d.uniforms.size(); u++)
		{
			double v[4];
			CurrentValues(d, u, v);
			Printf("  %s : %f %f %f\n", d.uniforms[u].name.c_str(), v[0], v[1], v[2]);
		}
	}
	if (!found) Printf("No shader named '%s' found\n", argv[1]);
}
