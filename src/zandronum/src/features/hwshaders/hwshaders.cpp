// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwshaders/hwshaders.h"
#include "features/hwshaders/computation/hwshader_compute.h"
#include "features/postprocess/postprocess.h"
#include "features/actormaterials/actormaterials.h"   // [Doruk-Aksoy] the fua_Material declarations

#include "doomtype.h"
#include "zstring.h"
#include "tarray.h"
#include "name.h"
#include "sc_man.h"
#include "c_console.h"
#include "w_wad.h"
#include "textures/textures.h"

#include <array>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace zx { namespace hwshaders {

static std::vector<UserShaderKey> g_userShaders;

// [Doruk-Aksoy] Each texture's layers past its colour and brightmap (MaterialLayer), from its block's Texture lines and, later, its Material block's maps.
static std::unordered_map<const FTexture *, std::array<FTexture *, kLayerCount>> g_layers;

// [Doruk-Aksoy] Each texture's glossiness and specular level as ComputeSpecularWord packs them, from its Material block.
static std::unordered_map<const FTexture *, unsigned> g_specular;

static void SetLayer(FTexture *tex, int layer, FTexture *with)
{
	auto it = g_layers.find(tex);
	if (it == g_layers.end()) it = g_layers.emplace(tex, std::array<FTexture *, kLayerCount>()).first;
	it->second[layer] = with;
}

// [Doruk-Aksoy] Built-in shaders by material type, made when a Material block's maps first give that type.
static int g_builtin[3] = { -1, -1, -1 };

// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad gldefs.cpp ParseMaterial's map keywords, in MaterialLayers' order, the brightmap first.
static const char *const kMapKeywords[] = { "brightmap", "normal", "specular", "metallic", "roughness", "ao" };
static const int kMapLayers[] = { -1, kLayerNormal, kLayerSpecular, kLayerMetallic, kLayerRoughness, kLayerAO };

// [Doruk-Aksoy] Whether a texture `thiswad` or `iwad` limits a block to is one of those: its lump in the GLDEFS lump's wad, or in the IWAD.
static bool BlockReaches(FTexture *tex, bool thiswad, bool iwad, int deflump)
{
	if (!thiswad && !iwad) return true;
	const int lumpnum = tex->GetSourceLump();
	if (lumpnum == -1) return false;
	return (iwad && Wads.GetLumpFile(lumpnum) <= FWadCollection::IWAD_FILENUM) || (thiswad && Wads.GetLumpFile(lumpnum) == deflump);
}

// [Doruk-Aksoy] The type a texture's maps give it.
static MaterialType MapsType(const FTexture *tex)
{
	return ComputeMaterialTypeFromLayers(LayerTexture(tex, kLayerNormal) != NULL, LayerTexture(tex, kLayerSpecular) != NULL,
		LayerTexture(tex, kLayerMetallic) != NULL, LayerTexture(tex, kLayerRoughness) != NULL, LayerTexture(tex, kLayerAO) != NULL);
}

// [Doruk-Aksoy] A Material block's maps put on its texture: the brightmap as gl_ParseBrightmap puts one, the rest as layers, and its type's built-in shader made.
static MaterialType ApplyMaps(FTexture *tex, FTexture *const (&maps)[6], bool hasGloss, float gloss, bool hasLevel, float level)
{
	if (maps[0] != NULL)
	{
		maps[0]->bMasked = false;
		tex->gl_info.Brightmap = maps[0];
	}
	for (int i = 1; i < 6; i++)
		if (maps[i] != NULL) SetLayer(tex, kMapLayers[i], maps[i]);
	if (hasGloss || hasLevel) g_specular[tex] = ComputeSpecularWord(hasGloss, gloss, hasLevel, level);
	const MaterialType type = MapsType(tex);
	if (type != kMaterialDefault && g_builtin[type] < 0) g_builtin[type] = ComputeUserShaderSlot(g_userShaders, ComputeBuiltinMaterialKey(type));
	return type;
}

// [Doruk-Aksoy] One of the lines every shader block shares, Shader, Define and fua_ProcessLighting, into its key; false for any other.
static bool ParseKeyLine(FScanner &sc, UserShaderKey &key)
{
	if (sc.Compare("shader"))
	{
		sc.MustGetString();
		key.lump = sc.String;
	}
	else if (sc.Compare("fua_ProcessLighting"))
	{
		sc.MustGetString();
		if (!ComputeProcessLightingOf(sc.String, key.lighting))
			sc.ScriptMessage("Unknown fua_ProcessLighting '%s', expected zandronum or uzdoom; keeping zandronum\n", sc.String);
	}
	else if (sc.Compare("define"))
	{
		sc.MustGetString();
		ShaderDefine d;
		d.name = sc.String;
		if (sc.CheckToken('='))
		{
			sc.MustGetString();
			d.value = sc.String;
		}
		key.defines.push_back(d);
	}
	else return false;
	return true;
}

// [Doruk-Aksoy] Each fua_Material's shader, by material.
static std::vector<int> g_actorShaders;

// [Doruk-Aksoy] A HardwareShader block or (material) a Material one, which UZDoom reads alike but for the maps and settings only a Material block has.
static void ParseShaderBlock(FScanner &sc, int deflump, bool material)
{
	int type = FTexture::TEX_Any;
	UserShaderKey key;
	float speed = 1.f;
	bool disableAlphaTest = false;
	std::vector<FTexture *> custom;
	FTexture *maps[6] = {};
	bool hasGloss = false, hasLevel = false, thiswad = false, iwad = false, disableFullbright = false;
	float gloss = 0.f, level = 0.f;

	sc.MustGetString();
	if (sc.Compare("texture")) type = FTexture::TEX_Wall;
	else if (sc.Compare("flat")) type = FTexture::TEX_Flat;
	else if (sc.Compare("sprite")) type = FTexture::TEX_Sprite;
	else sc.UnGet();

	sc.MustGetString();
	FTextureID no = TexMan.CheckForTexture(sc.String, type);
	FTexture *tex = TexMan[no];
	if (tex == NULL && material) sc.ScriptMessage("Material definition refers nonexistent texture '%s'\n", sc.String);

	sc.MustGetToken('{');
	while (!sc.CheckToken('}'))
	{
		sc.MustGetString();
		int map = -1;
		for (int i = 0; material && i < 6; i++) if (sc.Compare(kMapKeywords[i])) map = i;
		if (map >= 0)
		{
			sc.MustGetString();
			if (maps[map] != NULL) sc.ScriptMessage("Multiple %s definitions in texture '%s'\n", kMapKeywords[map], tex ? tex->Name.GetChars() : "(null)");
			maps[map] = TexMan.FindTexture(sc.String, FTexture::TEX_Any, FTextureManager::TEXMAN_TryAny);
			if (maps[map] == NULL) sc.ScriptMessage("%s '%s' not found in texture '%s'\n", kMapKeywords[map], sc.String, tex ? tex->Name.GetChars() : "(null)");
		}
		else if (material && sc.Compare("glossiness"))
		{
			sc.MustGetFloat();
			hasGloss = true;
			gloss = float(sc.Float);
		}
		else if (material && sc.Compare("specularlevel"))
		{
			sc.MustGetFloat();
			hasLevel = true;
			level = float(sc.Float);
		}
		else if (material && sc.Compare("thiswad")) thiswad = true;
		else if (material && sc.Compare("iwad")) iwad = true;
		else if (material && sc.Compare("disablefullbright")) disableFullbright = true;
		// [Doruk-Aksoy] Every texture here is mipmapped as GL's own are, so nomipmap is read and left.
		else if (sc.Compare("nomipmap")) {}
		else if (ParseKeyLine(sc, key)) {}
		else if (sc.Compare("speed"))
		{
			sc.MustGetFloat();
			speed = float(sc.Float);
		}
		// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad gldefs.cpp ParseHardwareShader: the type that numbers the samplers and picks the light model.
		else if (sc.Compare("material"))
		{
			sc.MustGetString();
			if (!ComputeMaterialTypeOf(sc.String, key.type))
				sc.ScriptMessage("Unknown material type '%s' specified\n", sc.String);
		}
		else if (sc.Compare("texture"))
		{
			sc.MustGetString();
			const std::string name = sc.String;
			sc.MustGetString();
			FTexture *layer = TexMan.FindTexture(sc.String, FTexture::TEX_Any, FTextureManager::TEXMAN_TryAny);
			if (layer == NULL)
				sc.ScriptMessage("Custom hardware shader texture '%s' not found in texture '%s'\n", sc.String, tex ? tex->Name.GetChars() : "(null)");
			else if (const char *why = ComputeCustomTextureRefusal(key.textures, name, ComputeCustomTextureLimit(key.type)))
				sc.ScriptMessage("Custom hardware shader texture '%s' %s in texture '%s'\n", name.c_str(), why, tex ? tex->Name.GetChars() : "(null)");
			else
			{
				key.textures.push_back(name);
				custom.push_back(layer);
			}
		}
		else if (sc.Compare("disablealphatest"))
			disableAlphaTest = true;
		// [Doruk-Aksoy] Uniforms scripts set by the set's name with fua_SetShaderUniform, shared by every block naming it.
		else if (sc.Compare("fua_Name"))
		{
			sc.MustGetString();
			key.uniformSet = sc.String;
		}
		else if (sc.Compare("fua_Uniform"))
			zx::postprocess::ParseTextureShaderUniform(sc, key.uniformSet);
		else if (material)
			sc.ScriptMessage("Unknown keyword '%s' in texture '%s'\n", sc.String, tex ? tex->Name.GetChars() : "(null)");
	}
	if (!tex || !BlockReaches(tex, thiswad, iwad, deflump)) return;
	if (material)
	{
		if (disableFullbright) tex->gl_info.bDisableFullbright = true;
		// [Doruk-Aksoy] A Material block's shader is of the type its maps give, as it cannot name one.
		key.type = ApplyMaps(tex, maps, hasGloss, gloss, hasLevel, level);
	}
	if (key.lump.empty()) return;
	if (tex->bWarped != 0)
	{
		Printf("Cannot combine warping with hardware shader on texture '%s'\n", tex->Name.GetChars());
		return;
	}
	// [Doruk-Aksoy] A type's limit is checked once the block is read, since `Material` may follow the Texture lines it numbers.
	const int limit = ComputeCustomTextureLimit(key.type);
	if ((int)key.textures.size() > limit)
	{
		sc.ScriptMessage("Error: out of texture units in texture '%s'\n", tex->Name.GetChars());
		key.textures.resize(limit);
		custom.resize(limit);
	}
	tex->gl_info.shaderspeed = speed;
	tex->gl_info.shaderindex = ComputeUserShaderSlot(g_userShaders, key) + kFirstUserShader;
	// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad gldefs.cpp SetShaderIndex: disablealphatest makes the texture translucent.
	if (disableAlphaTest) tex->gl_info.mIsTransparent = 1;
	for (int i = kLayerCustom; i < kLayerCount; i++) SetLayer(tex, i, i - kLayerCustom < (int)custom.size() ? custom[i - kLayerCustom] : NULL);
}

void ParseHardwareShader(FScanner &sc, int deflump)
{
	sc.MustGetString();
	// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad: a post-process shader is a block of its own, not a texture's.
	if (sc.Compare("postprocess"))
	{
		zx::postprocess::ParsePostProcessShader(sc);
		return;
	}
	sc.UnGet();
	ParseShaderBlock(sc, deflump, false);
}

void ParseMaterial(FScanner &sc, int deflump)
{
	ParseShaderBlock(sc, deflump, true);
}

void ParseActorMaterial(FScanner &sc)
{
	sc.MustGetString();
	const std::string name = sc.String;
	const int material = zx::actormaterials::Declare(name.c_str());
	if (material < 0) sc.ScriptError("Too many fua_Material blocks: %d at most", (int)zx::actormaterials::kMaxActorMaterials);
	UserShaderKey key;
	key.actorMaterial = material;
	sc.MustGetToken('{');
	while (!sc.CheckToken('}'))
	{
		sc.MustGetString();
		if (ParseKeyLine(sc, key)) continue;
		if (sc.Compare("fua_Uniform")) zx::actormaterials::ParseUniform(sc, material);
		else sc.ScriptMessage("Unknown keyword '%s' in fua_Material '%s'\n", sc.String, name.c_str());
	}
	if (key.lump.empty())
	{
		sc.ScriptMessage("fua_Material '%s' names no Shader\n", name.c_str());
		return;
	}
	if ((int)g_actorShaders.size() <= material) g_actorShaders.resize(material + 1, -1);
	g_actorShaders[material] = ComputeUserShaderSlot(g_userShaders, key);
}

int ActorMaterialShader(int material)
{
	return material >= 0 && material < (int)g_actorShaders.size() && material < zx::actormaterials::MaterialCount() ? g_actorShaders[material] : -1;
}

int TextureUserShader(const FTexture *tex)
{
	const int declared = UserShaderOf(tex->gl_info.shaderindex);
	const MaterialType inferred = MapsType(tex);
	return ComputeMaterialShader(declared, declared >= 0 ? g_userShaders[declared].type : kMaterialDefault, inferred, g_builtin[inferred]);
}

int UserShaderCount() { return (int)g_userShaders.size(); }

const UserShaderKey *UserShaderKeyOf(int i)
{
	if (i < 0 || i >= UserShaderCount()) return NULL;
	// [Doruk-Aksoy] A set's uniforms are known once every block naming it is read, so they are taken when a program is made.
	UserShaderKey &key = g_userShaders[i];
	key.uniforms.clear();
	// [Doruk-Aksoy] A fua_Material's uniforms are read from the drawn actor's row, a vec4 each in declaration order.
	if (key.actorMaterial >= 0 && key.actorMaterial < zx::actormaterials::MaterialCount())
	{
		const std::vector<zx::actormaterials::UniformDecl> &decls = zx::actormaterials::Uniforms(key.actorMaterial);
		for (size_t s = 0; s < decls.size(); s++)
		{
			ShaderUniform u;
			u.name = decls[s].name;
			u.components = decls[s].components;
			u.isInt = decls[s].isInt;
			u.slot = (int)s;
			u.perActor = true;
			key.uniforms.push_back(u);
		}
		return &key;
	}
	if (const zx::postprocess::ShaderDecl *set = key.uniformSet.empty() ? NULL : zx::postprocess::TextureShaderSet(key.uniformSet.c_str()))
		for (const zx::postprocess::UniformField &f : set->uniforms)
		{
			ShaderUniform u;
			u.name = f.name;
			u.components = zx::postprocess::ComputeUniformComponents(f.type);
			u.isInt = f.type == zx::postprocess::kUniformInt;
			u.slot = (int)(f.offset / 16);
			key.uniforms.push_back(u);
		}
	return &key;
}

const char *UserShaderLump(int i)
{
	return (i >= 0 && i < UserShaderCount()) ? g_userShaders[i].lump.c_str() : "";
}

ProcessLighting UserShaderLighting(int i)
{
	return (i >= 0 && i < UserShaderCount()) ? g_userShaders[i].lighting : kLightingZandronum;
}

int UserShaderOf(int shaderIndex)
{
	const int i = shaderIndex - kFirstUserShader;
	return (i >= 0 && i < UserShaderCount()) ? i : -1;
}

FTexture *LayerTexture(const FTexture *tex, int layer)
{
	if (layer < 0 || layer >= kLayerCount) return NULL;
	auto it = g_layers.find(tex);
	return it == g_layers.end() ? NULL : it->second[layer];
}

bool HasLayers(const FTexture *tex)
{
	auto it = g_layers.find(tex);
	if (it == g_layers.end()) return false;
	for (FTexture *t : it->second) if (t != NULL) return true;
	return false;
}

unsigned SpecularWord(const FTexture *tex)
{
	auto it = g_specular.find(tex);
	return it == g_specular.end() ? 0u : it->second;
}

}} // namespace zx::hwshaders
