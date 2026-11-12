// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/postprocess/postprocess.h"
#include "features/handoff/effects.h"   // [Doruk-Aksoy] QueuedBesideTic
#include "features/hwshaders/computation/hwshader_compute.h"   // [Doruk-Aksoy] kTextureUniformSlots

#include "doomtype.h"
#include "doomstat.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "g_level.h"
#include "sc_man.h"
#include "w_wad.h"

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>

// [Doruk-Aksoy] Ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad: off skips every custom post-process shader.
CVAR(Bool, gl_custompost, true, CVAR_FUA_DEV_MODE)

namespace zx { namespace postprocess {

namespace {

std::vector<ShaderDecl> g_shaders;
// [Doruk-Aksoy] Texture shaders' fua_Uniform sets by fua_Name, each uniform at its shared vec4's offset.
std::vector<ShaderDecl> g_textureSets;
int g_textureUniforms = 0;
unsigned g_generation = 0;
std::string g_gameDefsLump;
DrawnShadersSource g_drawnSource = NULL;

// [Doruk-Aksoy] What the frame draws, from the copy SetDrawnShaders installed, or nothing before it is.
DrawnShaders Drawn()
{
	static const ShaderTable none;
	if (g_drawnSource != NULL) return g_drawnSource();
	const DrawnShaders empty = { &none, 0 };
	return empty;
}

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
	// [Doruk-Aksoy] A texture shader set has uniforms to set but nothing to turn on or off.
	for (const ShaderDecl &d : g_textureSets)
	{
		if (d.name != c->shader || !c->setUniform) continue;
		found = true;
		ComputeSetShaderUniform(level.fua_postShaders, d.name, -1, c->uniform, c->values);
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
	if (const double *set = ComputeEffectiveUniform(*Drawn().table, d.name, consoleplayer, d.uniforms[i].name))
	{
		memcpy(out, set, sizeof(double) * 4);
		return;
	}
	if (d.cvars[i] != NULL) { CvarValues(d.cvars[i], out); return; }
	for (int k = 0; k < 4; k++) out[k] = d.defaults[i][k];
}

// [Doruk-Aksoy] Each of d's uniforms with its current values into its place in block.
void WriteUniforms(const ShaderDecl &d, unsigned char *block)
{
	for (size_t u = 0; u < d.uniforms.size(); u++)
	{
		double values[4];
		CurrentValues(d, u, values);
		ComputeWriteUniform(block, d.uniforms[u], values);
	}
}

// [Doruk-Aksoy] Each uniform of d by name with its current values, for fua_postshaders.
std::vector<UniformState> UniformStates(const ShaderDecl &d)
{
	std::vector<UniformState> out(d.uniforms.size());
	for (size_t u = 0; u < d.uniforms.size(); u++)
	{
		out[u].name = d.uniforms[u].name;
		CurrentValues(d, u, out[u].values);
	}
	return out;
}

ShaderDecl *FindTextureSet(const char *name)
{
	for (ShaderDecl &d : g_textureSets)
		if (stricmp(d.name.c_str(), name) == 0) return &d;
	return NULL;
}

// [Doruk-Aksoy] A rebuilt texture block (ComputePostProcessDeclarations): its fua_Name, then its fua_Uniform lines.
void ParseTextureSetStatements(FScanner &sc)
{
	std::string set;
	sc.MustGetToken('{');
	while (!sc.CheckToken('}'))
	{
		sc.MustGetString();
		if (sc.Compare("fua_name"))
		{
			sc.MustGetString();
			set = sc.String;
		}
		else ParseTextureShaderUniform(sc, set);
	}
}

} // namespace

void ParseTextureShaderUniform(FScanner &sc, const std::string &set)
{
	if (set.empty()) sc.ScriptError("fua_Uniform needs the block's fua_Name before it");
	sc.MustGetString();
	const UniformType type = ComputeUniformType(sc.String);
	if (type == kUniformUndefined) sc.ScriptError("Unrecognized uniform type '%s'", sc.String);
	sc.MustGetString();
	const std::string name = sc.String;
	double values[4] = { 0.0, 0.0, 0.0, 1.0 };
	FBaseCVar *cvar = NULL;
	ParseUniformValues(sc, type, set, values, cvar);

	ShaderDecl *d = FindTextureSet(set.c_str());
	if (d == NULL)
	{
		g_textureSets.push_back(ShaderDecl());
		d = &g_textureSets.back();
		d->name = set;
	}
	bool known = false;
	for (const UniformField &u : d->uniforms)
	{
		if (u.name != name) continue;
		if (u.type != type) sc.ScriptError("fua_Uniform '%s' of '%s' is declared again as another type", name.c_str(), set.c_str());
		known = true;
	}
	if (!known && g_textureUniforms >= zx::hwshaders::kTextureUniformSlots)
		sc.ScriptError("Too many fua_Uniform lines: texture shaders share %d", (int)zx::hwshaders::kTextureUniformSlots);
	AddUniform(*d, name, type, values, cvar);
	// [Doruk-Aksoy] A new uniform takes the next shared vec4; one declared again keeps its own.
	if (!known) d->uniforms.back().offset = 16u * (unsigned)g_textureUniforms++;
	g_generation++;
}

const ShaderDecl *TextureShaderSet(const char *name) { return FindTextureSet(name); }

bool TextureUniformsDeclared() { return g_textureUniforms > 0; }

void TextureUniformBlock(unsigned char *block)
{
	memset(block, 0, 16u * zx::hwshaders::kTextureUniformSlots);
	for (const ShaderDecl &d : g_textureSets) WriteUniforms(d, block);
}

void ClearPostProcessShaders(const char *gameDefsLump)
{
	g_shaders.clear();
	g_textureSets.clear();
	g_textureUniforms = 0;
	g_generation++;
	g_gameDefsLump = gameDefsLump != NULL ? gameDefsLump : "";
}

void SetDrawnShaders(DrawnShadersSource source) { g_drawnSource = source; }

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

void NotePostProcessRenderer(bool backendDraws)
{
	if (!g_shaders.empty() && !backendDraws)
		Printf("%d post-process shaders draw only on the Vulkan renderer\n", (int)g_shaders.size());
}

namespace {

// [Doruk-Aksoy] One GLDEFS lump's parts and those of the lumps it includes, each handed to `use`, `depth` stopping an include loop.
void PartsFrom(int lump, int depth, DefsPartReader use)
{
	if (depth > 16) return;
	FMemLump data = Wads.ReadLump(lump);
	const std::string text((const char *)data.GetMem(), data.GetSize());
	for (const DeclarationPart &part : ComputePostProcessDeclarations(text))
	{
		if (!part.include) { use(lump, part); continue; }
		const int included = Wads.CheckNumForFullName(part.text.c_str(), true);
		if (included >= 0) PartsFrom(included, depth + 1, use);
	}
}

void PartsNamed(const char *name, DefsPartReader use)
{
	int lump, last = 0;
	while ((lump = Wads.FindLump(name, &last)) != -1) PartsFrom(lump, 0, use);
}

// [Doruk-Aksoy] A post-process block or a texture block's fua_ lines; a fua_Material block is features/actormaterials'.
void ReadPostProcessPart(int lump, const DeclarationPart &part)
{
	if (!part.material.empty()) return;
	FScanner sc;
	sc.OpenMem(Wads.GetLumpFullName(lump), part.text.c_str(), (int)part.text.size());
	if (part.texture) ParseTextureSetStatements(sc);
	else ParsePostProcessShader(sc);
}

void RereadDeclarations(intptr_t, intptr_t)
{
	const std::string game = g_gameDefsLump;
	ReadPostProcessDeclarations(game.empty() ? NULL : game.c_str());
	Printf("fua_postshaders_reread: %d declared\n", PostShaderCount());
}

} // namespace

void ForEachDefsPart(const char *gameDefsLump, DefsPartReader use)
{
	if (gameDefsLump == NULL && !g_gameDefsLump.empty()) gameDefsLump = g_gameDefsLump.c_str();
	if (gameDefsLump != NULL) PartsNamed(gameDefsLump, use);
	PartsNamed("GLDEFS", use);
}

void ReadPostProcessDeclarations(const char *gameDefsLump)
{
	ClearPostProcessShaders(gameDefsLump);
	ForEachDefsPart(gameDefsLump, &ReadPostProcessPart);
}

bool PostShaderDeclared(const char *shader, const char *uniform)
{
	// [Doruk-Aksoy] Texture shader sets count only for a uniform, as they have nothing to turn on.
	static std::vector<DeclaredShader> declared, withTextures;
	static unsigned madeFor = ~0u;
	if (madeFor != g_generation)
	{
		const auto add = [](const std::vector<ShaderDecl> &list, std::vector<DeclaredShader> &out) {
			for (const ShaderDecl &d : list)
			{
				DeclaredShader s = { d.name, {} };
				for (const UniformField &u : d.uniforms) s.uniforms.push_back(u.name);
				out.push_back(s);
			}
		};
		declared.clear();
		add(g_shaders, declared);
		withTextures = declared;
		add(g_textureSets, withTextures);
		madeFor = g_generation;
	}
	return ComputeShaderCallDeclared(uniform[0] != '\0' ? withTextures : declared, shader, uniform);
}

int PostShaderCount() { return (int)g_shaders.size(); }
const ShaderDecl &PostShader(int i) { return g_shaders[i]; }
unsigned PostShadersGeneration() { return g_generation; }
bool PostProcessAllowed() { return gl_custompost; }

EffectiveEnabled PostShaderEnabledNow(int i)
{
	const ShaderDecl &d = g_shaders[i];
	const DrawnShaders drawn = Drawn();
	return ComputeEffectiveEnabled(*drawn.table, d.name, consoleplayer, d.enabled, drawn.maptime);
}

void PostShaderBlock(int i, unsigned char *block)
{
	memset(block, 0, g_shaders[i].blockSize);
	WriteUniforms(g_shaders[i], block);
}

}} // namespace zx::postprocess

using namespace zx::postprocess;

// [Doruk-Aksoy] Ported from uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad (hw_postprocessshader_ccmds.cpp): the console's post-process commands, which change the level as a script would.
DEV_MODE_CCMD(shaderenable)
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

DEV_MODE_CCMD(shaderuniform)
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
DEV_MODE_CCMD(fua_postshaders)
{
	const DrawnShaders drawn = Drawn();
	Printf("fua_postshaders: %d entries at %d\n", (int)drawn.table->entries.size(), drawn.maptime);
	for (int i = 0; i < PostShaderCount(); i++)
		Printf("fua_postshaders: %s\n", ComputeShaderStateLine(PostShader(i).name, PostShaderEnabledNow(i), UniformStates(PostShader(i))).c_str());
	for (const ShaderDecl &d : g_textureSets)
		Printf("fua_postshaders: %s\n", ComputeTextureSetStateLine(d.name, UniformStates(d)).c_str());
}

// [Doruk-Aksoy] Reads GLDEFS's post-process declarations again as a dedicated server's null renderer does, so a netgame test checks that path on any build.
DEV_MODE_CCMD(fua_postshaders_reread)
{
	const zx::handoff::UIEffect effect = { &RereadDeclarations, 0, 0 };
	if (!zx::handoff::QueuedBesideTic(effect)) effect.run(effect.a, effect.b);
}

DEV_MODE_CCMD(listshaders)
{
	for (int i = 0; i < PostShaderCount(); i++) Printf("Shader (%i): %s\n", i, PostShader(i).name.c_str());
}

DEV_MODE_CCMD(listuniforms)
{
	if (argv.argc() < 2)
	{
		Printf("Usage: listuniforms [name]\n");
		return;
	}
	bool found = false;
	for (const std::vector<ShaderDecl> *list : { &g_shaders, &g_textureSets })
	for (const ShaderDecl &d : *list)
	{
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
