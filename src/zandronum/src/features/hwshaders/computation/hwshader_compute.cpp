// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwshaders/computation/hwshader_compute.h"

#include <algorithm>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace zx { namespace hwshaders {

int ComputePieceShader(int warp, int userShader)
{
	if (warp == 1 || warp == 2) return warp;
	return userShader >= 0 ? 3 + userShader : 0;
}

int ComputeUserShaderOf(int pieceShader)
{
	return pieceShader >= 3 ? pieceShader - 3 : -1;
}

int ComputeDrawTextureShader(int materialShader)
{
	return materialShader >= kFirstUserShader ? -1 : 0;
}

namespace {

bool IdentChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

// [Doruk-Aksoy] Whether s[at, at + len) stands as a whole word.
bool WholeAt(const std::string &s, size_t at, size_t len)
{
	return (at == 0 || !IdentChar(s[at - 1])) && (at + len >= s.size() || !IdentChar(s[at + len]));
}

// [rc4l] s with every whole-word `word` replaced by `with`.
std::string ReplaceWord(const std::string &s, const std::string &word, const std::string &with)
{
	std::string out;
	size_t i = 0;
	while (i < s.size())
	{
		const size_t at = s.find(word, i);
		if (at == std::string::npos) { out.append(s, i, std::string::npos); break; }
		const size_t end = at + word.size();
		out.append(s, i, at - i);
		out += WholeAt(s, at, word.size()) ? with : word;
		i = end;
	}
	return out;
}

// [rc4l] s with every `what` replaced by `with`, whatever surrounds it.
std::string ReplaceAll(const std::string &s, const std::string &what, const std::string &with)
{
	std::string out;
	size_t i = 0;
	for (size_t at; (at = s.find(what, i)) != std::string::npos; i = at + what.size())
		out.append(s, i, at - i) += with;
	return out.append(s, i, std::string::npos);
}

// [Doruk-Aksoy] Whether line, its spaces taken out, is one of `decls`.
bool IsDeclaration(const std::string &line, std::initializer_list<const char *> decls)
{
	std::string t;
	for (char c : line) if (!isspace((unsigned char)c)) t += c;
	for (const char *d : decls) if (t == d) return true;
	return false;
}

// [Doruk-Aksoy] source without the lines `drop` picks, each line kept ending in a newline.
template <class Drop> std::string KeepLines(const std::string &source, Drop drop)
{
	std::string kept;
	size_t i = 0;
	while (i < source.size())
	{
		size_t end = source.find('\n', i);
		if (end == std::string::npos) end = source.size();
		const std::string line = source.substr(i, end - i);
		if (!drop(line)) kept += line + "\n";
		i = end + 1;
	}
	return kept;
}

// [Doruk-Aksoy] Whether `word` stands whole somewhere in s.
bool HasWord(const std::string &s, const std::string &word)
{
	for (size_t at = s.find(word); at != std::string::npos; at = s.find(word, at + 1))
		if (WholeAt(s, at, word.size())) return true;
	return false;
}

// [Doruk-Aksoy] Where the spaces and tabs before s[at] begin.
size_t BackOverSpaces(const std::string &s, size_t at)
{
	while (at > 0 && (s[at - 1] == ' ' || s[at - 1] == '\t')) at--;
	return at;
}

// [Doruk-Aksoy] Whether s has `name` of its own: a `#define name` line, or a float or vector declared under it.
bool OwnsName(const std::string &s, const std::string &name)
{
	for (size_t at = s.find(name); at != std::string::npos; at = s.find(name, at + 1))
	{
		if (!WholeAt(s, at, name.size())) continue;
		const size_t b = BackOverSpaces(s, at);
		size_t a = b;
		while (a > 0 && IdentChar(s[a - 1])) a--;
		const std::string before = s.substr(a, b - a);
		const size_t hash = BackOverSpaces(s, a);
		if ((before == "define" && hash > 0 && s[hash - 1] == '#') || before == "float" || before == "vec2" || before == "vec3" || before == "vec4")
			return true;
	}
	return false;
}

} // namespace

std::string ComputeRewriteIndexedName(const std::string &source, const std::string &name, const std::string &func)
{
	std::string out;
	size_t i = 0;
	for (size_t at; (at = source.find(name, i)) != std::string::npos;)
	{
		const size_t end = at + name.size();
		const size_t open = source.find_first_not_of(" \t", end);
		size_t close = source.size();
		if (WholeAt(source, at, name.size()) && open != std::string::npos && source[open] == '[')
		{
			int depth = 0;
			for (close = open; close < source.size(); close++)
			{
				if (source[close] == '[') depth++;
				else if (source[close] == ']' && --depth == 0) break;
			}
		}
		// [Doruk-Aksoy] Not an index, or one never closed, is left as it is.
		if (close >= source.size())
		{
			out.append(source, i, end - i);
			i = end;
			continue;
		}
		out.append(source, i, at - i);
		out += func + "(" + ComputeRewriteIndexedName(source.substr(open + 1, close - open - 1), name, func) + ")";
		i = close + 1;
	}
	return out.append(source, i, std::string::npos);
}

std::string ComputeLegacyShaderNames(const std::string &source)
{
	const std::string kept = KeepLines(source, [](const std::string &line) {
		return IsDeclaration(line, { "uniformvec3camerapos;", "varyingvec4pixelpos;", "invec4pixelpos;" });
	});
	const std::string rewritten = ComputeRewriteIndexedName(kept, "gl_TextureMatrix", "fuaTextureMatrix");
	std::string pre;
	if (HasWord(rewritten, "camerapos") && !OwnsName(rewritten, "camerapos"))
		pre += "#define camerapos (uCameraPos.xyz)\n";
	if (rewritten != kept)
		pre += "mat4 fuaTextureMatrix(int i) { return i == 7 ? ModelMatrix : (i == 0 ? TextureMatrix : mat4(1.0)); }\n";
	return pre + rewritten;
}

namespace {

// [Doruk-Aksoy] Whether a whole-word `name` in s is followed, past spaces and an opening bracket, by the whole word `first`.
bool FirstParameterIs(const std::string &s, const std::string &name, const std::string &first)
{
	for (size_t at = s.find(name); at != std::string::npos; at = s.find(name, at + 1))
	{
		if (!WholeAt(s, at, name.size())) continue;
		size_t i = s.find_first_not_of(" \t\r\n", at + name.size());
		if (i == std::string::npos || s[i] != '(') continue;
		i = s.find_first_not_of(" \t\r\n", i + 1);
		if (i != std::string::npos && s.compare(i, first.size(), first) == 0 && WholeAt(s, i, first.size())) return true;
	}
	return false;
}

bool SameIgnoringCase(const std::string &a, const std::string &b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
	return true;
}

} // namespace

ShaderEntries ComputeShaderEntries(const std::string &source)
{
	// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad gl_shader.cpp: each entry is looked for by name in the shader's text.
	ShaderEntries e;
	e.setupMaterial = HasWord(source, "SetupMaterial");
	e.processMaterial = HasWord(source, "ProcessMaterial");
	e.getTexCoord = HasWord(source, "GetTexCoord");
	e.processTexel = HasWord(source, "ProcessTexel");
	e.processLight = HasWord(source, "ProcessLight");
	e.materialLight = FirstParameterIs(source, "ProcessLight", "Material");
	return e;
}

bool ComputeProcessLightingOf(const std::string &value, ProcessLighting &out)
{
	if (SameIgnoringCase(value, "zandronum")) out = kLightingZandronum;
	else if (SameIgnoringCase(value, "uzdoom")) out = kLightingUZDoom;
	else return false;
	return true;
}

ProgramEntry ComputeProgramEntry(const ShaderEntries &e, ProcessLighting lighting)
{
	// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad gl_shader.cpp: SetupMaterial wins over an old ProcessMaterial, either over GetTexCoord, and that over ProcessTexel.
	if (e.setupMaterial) return kEntrySetupMaterial;
	if (e.processMaterial) return kEntryProcessMaterial;
	if (e.getTexCoord) return kEntryTexCoord;
	if (e.processTexel) return kEntryTexel;
	return lighting == kLightingUZDoom ? kEntryProcessLit : kEntryProcessFinal;
}

LightHook ComputeLightHook(const ShaderEntries &e)
{
	if (!e.processLight) return kLightNone;
	return e.materialLight ? kLightMaterial : kLightOld;
}

const char *ComputeGLEntryDefine(ProgramEntry entry)
{
	if (entry == kEntryProcessLit) return "#define FUA_PROCESS_UZDOOM\n";
	if (entry == kEntryProcessFinal) return "#define FUA_PROCESS_ZANDRONUM\n";
	return "";
}

bool ComputeMaterialTypeOf(const std::string &value, MaterialType &out)
{
	static const struct { const char *name; MaterialType type; } kTypes[] = {
		{ "normal", kMaterialDefault }, { "brightmap", kMaterialDefault },
		{ "specular", kMaterialSpecular }, { "specularbrightmap", kMaterialSpecular },
		{ "pbr", kMaterialPBR }, { "pbrbrightmap", kMaterialPBR },
	};
	for (const auto &t : kTypes)
		if (SameIgnoringCase(value, t.name)) { out = t.type; return true; }
	return false;
}

namespace {

// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad hw_material.cpp: each type's layers after its colour, texture2 first, up to the first custom one.
const int kNumberedEnd = 13;
const int kDefaultLayers[] = { kSamplerBrightmap, kLayerDetail, kLayerGlow };
const int kSpecularLayers[] = { kLayerNormal, kLayerSpecular, kSamplerBrightmap, kLayerDetail, kLayerGlow };
const int kPBRLayers[] = { kLayerNormal, kLayerMetallic, kLayerRoughness, kLayerAO, kSamplerBrightmap, kLayerDetail, kLayerGlow };

void LayersOf(MaterialType type, const int *&layers, int &count)
{
	if (type == kMaterialSpecular) { layers = kSpecularLayers; count = 5; }
	else if (type == kMaterialPBR) { layers = kPBRLayers; count = 7; }
	else { layers = kDefaultLayers; count = 3; }
}

} // namespace

unsigned ComputeSpecularWord(bool hasGlossiness, float glossiness, bool hasLevel, float level)
{
	const auto byteOf = [](bool has, float v) -> unsigned {
		if (!has) return 0;
		const long n = lround(v);
		return (unsigned)(n < 0 ? 0 : (n > 254 ? 254 : n)) + 1;
	};
	return byteOf(hasGlossiness, glossiness) | (byteOf(hasLevel, level * 100.f) << 8);
}

int ComputeCustomTextureLimit(MaterialType type)
{
	const int *layers;
	int count;
	LayersOf(type, layers, count);
	return kNumberedEnd - 2 - count;
}

int ComputeNumberedSampler(MaterialType type, int n)
{
	const int *layers;
	int count;
	LayersOf(type, layers, count);
	if (n < 2 || n >= kNumberedEnd) return kSamplerNone;
	const int i = n - 2;
	return i < count ? layers[i] : kLayerCustom + (i - count);
}

const char *ComputeCustomTextureRefusal(const std::vector<std::string> &names, const std::string &name, int limit)
{
	if (name.empty() || isdigit((unsigned char)name[0])) return "is not a name a shader can use";
	for (char c : name) if (!IdentChar(c)) return "is not a name a shader can use";
	for (const std::string &n : names) if (n == name) return "is defined twice";
	if ((int)names.size() >= limit) return "is past the last texture unit this shader type has";
	return nullptr;
}

MaterialType ComputeMaterialTypeFromLayers(bool normal, bool specular, bool metallic, bool roughness, bool ao)
{
	if (normal && specular) return kMaterialSpecular;
	if (normal && metallic && roughness && ao) return kMaterialPBR;
	return kMaterialDefault;
}

int ComputeMaterialShader(int declared, MaterialType declaredType, MaterialType inferred, int builtin)
{
	if (declared >= 0 && declaredType == inferred) return declared;
	return inferred != kMaterialDefault ? builtin : -1;
}

UserShaderKey ComputeBuiltinMaterialKey(MaterialType type)
{
	UserShaderKey k;
	k.type = type;
	return k;
}

const char *ComputeBuiltinMaterialSource(MaterialType type)
{
	// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad func_spec.fp and func_pbr.fp, the material each type's default shader fills.
	if (type == kMaterialSpecular)
		return "void SetupMaterial(inout Material material)\n{\n\tSetMaterialProps(material, vTexCoord.st);\n"
			"\tmaterial.Specular = texture(speculartexture, vTexCoord.st).rgb;\n"
			"\tmaterial.Glossiness = uSpecularMaterial.x;\n\tmaterial.SpecularLevel = uSpecularMaterial.y;\n}\n";
	if (type == kMaterialPBR)
		return "void SetupMaterial(inout Material material)\n{\n\tSetMaterialProps(material, vTexCoord.st);\n"
			"\tmaterial.Metallic = texture(metallictexture, vTexCoord.st).r;\n"
			"\tmaterial.Roughness = texture(roughnesstexture, vTexCoord.st).r;\n"
			"\tmaterial.AO = texture(aotexture, vTexCoord.st).r;\n}\n";
	return "void SetupMaterial(inout Material material)\n{\n\tSetMaterialProps(material, vTexCoord.st);\n}\n";
}

std::string ComputeUserShaderName(const UserShaderKey &key)
{
	if (!key.lump.empty()) return key.lump;
	return key.type == kMaterialPBR ? "the built-in PBR material" : key.type == kMaterialSpecular ? "the built-in specular material" : "the built-in material";
}

bool ComputeSameUserShader(const UserShaderKey &a, const UserShaderKey &b)
{
	if (a.lighting != b.lighting || a.type != b.type || a.textures != b.textures || a.defines.size() != b.defines.size() || a.uniforms.size() != b.uniforms.size()) return false;
	for (size_t i = 0; i < a.defines.size(); i++)
		if (a.defines[i].name != b.defines[i].name || a.defines[i].value != b.defines[i].value) return false;
	for (size_t i = 0; i < a.uniforms.size(); i++)
	{
		const ShaderUniform &x = a.uniforms[i], &y = b.uniforms[i];
		if (x.name != y.name || x.components != y.components || x.isInt != y.isInt || x.slot != y.slot || x.perActor != y.perActor) return false;
	}
	return a.actorMaterial == b.actorMaterial && SameIgnoringCase(a.lump, b.lump) && SameIgnoringCase(a.uniformSet, b.uniformSet);
}

int ComputeUserShaderSlot(std::vector<UserShaderKey> &shaders, const UserShaderKey &key)
{
	for (size_t i = 0; i < shaders.size(); i++)
		if (ComputeSameUserShader(shaders[i], key)) return (int)i;
	shaders.push_back(key);
	return (int)shaders.size() - 1;
}

std::string ComputeDefineLines(const std::vector<ShaderDefine> &defines)
{
	std::string out;
	for (const ShaderDefine &d : defines) out += "#define " + d.name + (d.value.empty() ? "" : " " + d.value) + "\n";
	return out;
}

std::string ComputeUndefineLines(const std::vector<ShaderDefine> &defines)
{
	std::string out;
	for (const ShaderDefine &d : defines) out += "#undef " + d.name + "\n";
	return out;
}

std::vector<ShaderDefine> ComputeMaterialTypeDefines(MaterialType type)
{
	if (type == kMaterialSpecular) return { { "SPECULAR", "" }, { "NORMALMAP", "" } };
	if (type == kMaterialPBR) return { { "PBR", "" }, { "NORMALMAP", "" } };
	return {};
}

namespace {

// [Doruk-Aksoy] A uniform's components as an index 0 to 3.
int ComponentIndex(const ShaderUniform &u) { return std::max(1, std::min(4, u.components)) - 1; }

} // namespace

const char *ComputeUniformTypeName(const ShaderUniform &u)
{
	static const char *const kFloats[] = { "float", "vec2", "vec3", "vec4" };
	return u.isInt ? "int" : kFloats[ComponentIndex(u)];
}

std::string ComputeUniformLines(const std::vector<ShaderUniform> &uniforms)
{
	static const char *const kSwizzles[] = { ".x", ".xy", ".xyz", "" };
	std::string out;
	for (const ShaderUniform &u : uniforms)
	{
		const std::string at = u.perActor ? "fuaActorUniform(" + std::to_string(u.slot) + ")" : "uFuaTexUniforms[" + std::to_string(u.slot) + "]";
		// [Doruk-Aksoy] A shared int is kept as its own bits, as ComputeWriteUniform writes it, and an actor's as a float.
		const std::string asInt = u.perActor ? " int(" + at + ".x)" : " floatBitsToInt(" + at + ".x)";
		out += "#define " + u.name + (u.isInt ? asInt : " (" + at + kSwizzles[ComponentIndex(u)] + ")") + "\n";
	}
	return out;
}

std::string ComputeUnuniformLines(const std::vector<ShaderUniform> &uniforms)
{
	std::string out;
	for (const ShaderUniform &u : uniforms) out += "#undef " + u.name + "\n";
	return out;
}

std::string ComputeDropUniformDeclarations(const std::string &source, const std::vector<ShaderUniform> &uniforms)
{
	if (uniforms.empty()) return source;
	return KeepLines(source, [&uniforms](const std::string &line) {
		for (const ShaderUniform &u : uniforms)
			if (IsDeclaration(line, { ("uniform" + std::string(ComputeUniformTypeName(u)) + u.name + ";").c_str() })) return true;
		return false;
	});
}

const char *ComputeGLRefusal(const UserShaderKey &key, ProgramEntry entry)
{
	if (entry == kEntryTexCoord || entry == kEntrySetupMaterial || entry == kEntryProcessMaterial)
		return "it uses UZDoom's SetupMaterial, ProcessMaterial or GetTexCoord, which only the Vulkan renderer runs";
	if (!key.textures.empty() || key.type != kMaterialDefault)
		return "it reads GLDEFS Texture lines or a Material type, which only the Vulkan renderer binds";
	if (!key.uniforms.empty() || key.actorMaterial >= 0) return "it reads fua_Uniform lines or is a fua_Material, which only the Vulkan renderer gives";
	return nullptr;
}

ShaderEntries ComputeAdaptUserShader(const std::string &source, int index, std::string &out)
{
	// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad hw_shaderpatcher.cpp RemoveLegacyUserUniforms: the loose timer and tex are the program's own.
	std::string kept = ComputeLegacyShaderNames(KeepLines(source, [](const std::string &line) {
		const size_t first = line.find_first_not_of(" \t\r");
		return (first != std::string::npos && line.compare(first, 8, "#version") == 0) || IsDeclaration(line, { "uniformfloattimer;", "uniformsampler2Dtex;" });
	}));
	const ShaderEntries entries = ComputeShaderEntries(kept);
	const std::string n = std::to_string(index);
	kept = ReplaceAll(kept, "gl_TexCoord[0]", "vTexCoord");
	for (const char *name : { "ProcessTexel", "ProcessLight", "Process", "SetupMaterial", "ProcessMaterial", "GetTexCoord" })
		kept = ReplaceWord(kept, name, std::string("fua") + name + "_" + n);
	out = kept;
	return entries;
}

namespace {

// [Doruk-Aksoy] What one kind of program hands a GLDEFS shader: GL main.fp's names for its own, its raw texel, how the entry is declared and guarded, its time, Zandronum 3.3's Process call with the alpha and shade that go with it, and the alpha the light hook hands ProcessLight.
struct ProgramNames
{
	const char *marker;
	const char *inputs;
	const char *getTexel, *raw, *desaturate, *worldNormal;
	std::vector<std::pair<const char *, const char *>> names;
	const char *entry, *guard, *timer, *lightAlpha;
	const char *finalCall, *finalAlpha, *finalShade;
	const char *plain;
};

const ProgramNames &NamesOf(ProgramKind kind)
{
	// [Doruk-Aksoy] Locations 23 to 26 are the model matrix's columns, which the scene vertex shaders write under FUA_USER_SHADER.
	// [Doruk-Aksoy] A warped piece (vWarp.x below 2.5) never runs the shader, so it is shaded as a plain one.
	static const ProgramNames kScene = {
		"//FUA_USER_SHADERS",
		"layout(location = 23) flat in vec4 vModel0;\n"
		"layout(location = 24) flat in vec4 vModel1;\n"
		"layout(location = 25) flat in vec4 vModel2;\n"
		"layout(location = 26) flat in vec4 vModel3;\n",
		"fuaTexMode(fuaTexel(st))", "fuaTexel", "vec4(fuaDesaturate(texel.rgb), texel.a)",
		"dot(vNormal, vNormal) > 0.0 ? normalize(vNormal) : vec3(0.0)",
		{ { "pixelpos", "vPixelPos" }, { "glowdist", "vGlowDist" }, { "uCameraPos", "vec4(uCameraPos.xyz, 0.0)" },
		  { "vColor", "vec4(vColor, vLightParm.z)" }, { "uDesaturationFactor", "vDesaturation" },
		  { "ModelMatrix", "mat4(vModel0, vModel1, vModel2, vModel3)" }, { "TextureMatrix", "mat4(1.0)" }, { "uSpecularMaterial", "fuaSpecularMaterial()" },
		  { "vWorldNormal", "vec4(fuaWorldNormal(), 0.0)" } },
		"vec4 fuaSurfaceTexel(vec2 fuaUV) {\n",
		"    if (vWarp.x < 2.5) return fuaTexMode(fuaTexel(fuaUV));\n",
		"uFrame.x * vWarp.y", "vLightParm.z",
		"@(vec4(fuaProcessLight(), vLightParm.z))", "vWarp.x < 2.5 ? vLightParm.z : 1.0", "vWarp.x < 2.5 ? fuaShade(t.rgb) : fuaShadeFinal(t.rgb)",
		"vec4 fuaSurfaceTexel(vec2 fuaUV) { return fuaTexMode(fuaTexel(fuaUV)); }\n"
		"float fuaSurfaceAlpha() { return vLightParm.z; }\n"
		"vec3 fuaSurfaceShade(vec4 t) { return fuaShade(t.rgb); }\n",
	};
	// [Doruk-Aksoy] The 2D layer's own vec4 vColor is GL's, so it is not renamed; a 2D draw has no world to place, and its colour is the light either contract hands or multiplies.
	static const ProgramNames k2D = {
		"//FUA_2D_SHADERS",
		"",
		"fua2DTexel(st)", "fua2DRaw", "texel", "vec3(0.0)",
		{ { "pixelpos", "vec4(gl_FragCoord.xy, 0.0, 0.0)" }, { "glowdist", "vec2(0.0)" }, { "uCameraPos", "vec4(0.0)" },
		  { "uDesaturationFactor", "0.0" }, { "ModelMatrix", "mat4(1.0)" }, { "TextureMatrix", "mat4(1.0)" }, { "uSpecularMaterial", "fuaSpecularMaterial()" },
		  { "vWorldNormal", "vec4(0.0)" } },
		"vec4 fua2DColor(vec2 fuaUV) {\n",
		"",
		"uInvScreen.z * vSpeed", "vColor.a",
		"@(vColor)", nullptr, nullptr,
		"vec4 fua2DColor(vec2 fuaUV) { return fua2DTexel(fuaUV) * vColor; }\n",
	};
	return kind == k2DProgram ? k2D : kScene;
}

// [Doruk-Aksoy] One sampler function, fuaSample_<name>, reading what UZDoom's sampler of that name holds.
std::string SamplerFunction(const std::string &name, int layer)
{
	const std::string l = std::to_string(layer);
	const std::string read = layer == kSamplerBrightmap ? "fuaBrightTexel(uv)" : "fuaLayerTexel(" + l + ", uv)";
	return "vec4 fuaSample_" + name + "(vec2 uv) { return " + read + "; }\n"
		"ivec2 fuaSize_" + name + "(int lod) { return fuaLayerSize(" + l + ", lod); }\n"
		"vec4 fuaFetch_" + name + "(ivec2 p, int lod) { return fuaLayerFetch(" + l + ", p, lod); }\n";
}

// [Doruk-Aksoy] UZDoom's samplers by every name a shader may use (tex, textureN, the named maps, the key's Texture lines), each a set of functions texture(), texture2D(), textureSize() and texelFetch() reach by the name pasted on.
std::string Samplers(const ProgramNames &p, const UserShaderKey &key)
{
	std::string b = std::string("vec4 fuaSample_tex(vec2 uv) { return ") + p.raw + "(uv); }\n"
		"ivec2 fuaSize_tex(int lod) { return fuaLayerSize(" + std::to_string(kSamplerBase) + ", lod); }\n"
		"vec4 fuaFetch_tex(ivec2 p, int lod) { return fuaLayerFetch(" + std::to_string(kSamplerBase) + ", p, lod); }\n";
	for (int n = 2; n < kNumberedEnd; n++)
	{
		const int layer = ComputeNumberedSampler(key.type, n);
		if (layer == kSamplerNone) continue;
		const int custom = layer - kLayerCustom;
		if (custom >= (int)key.textures.size()) continue;
		b += SamplerFunction("texture" + std::to_string(n), layer);
	}
	static const struct { const char *name; int layer; } kNamed[] = {
		{ "brighttexture", kSamplerBrightmap }, { "normaltexture", kLayerNormal }, { "speculartexture", kLayerSpecular },
		{ "metallictexture", kLayerMetallic }, { "roughnesstexture", kLayerRoughness }, { "aotexture", kLayerAO },
		{ "detailtexture", kLayerDetail }, { "glowtexture", kLayerGlow },
	};
	for (const auto &named : kNamed) b += SamplerFunction(named.name, named.layer);
	for (size_t i = 0; i < key.textures.size(); i++) b += SamplerFunction(key.textures[i], kLayerCustom + (int)i);
	return b + "#define texture2D(s, uv) fuaSample_##s(uv)\n#define texture(s, uv) fuaSample_##s(uv)\n"
		"#define textureSize(s, lod) fuaSize_##s(lod)\n#define texelFetch(s, p, lod) fuaFetch_##s(p, lod)\n";
}

// [Doruk-Aksoy] UZDoom main.fp's helpers a material shader calls: getTexel, desaturate, ApplyNormalMap (the normal map where the type has one) and SetMaterialProps, which reads no specular or PBR map, as UZDoom's does not.
std::string MaterialHelpers(const ProgramNames &p, MaterialType type)
{
	std::string b = std::string("vec4 getTexel(vec2 st) { return ") + p.getTexel + "; }\n"
		"vec4 desaturate(vec4 texel) { return " + p.desaturate + "; }\n"
		"vec3 fuaWorldNormal() { return " + p.worldNormal + "; }\n";
	if (type == kMaterialDefault)
		b += "vec3 ApplyNormalMap(vec2 texcoord) { return fuaWorldNormal(); }\n";
	else
		b += "vec3 ApplyNormalMap(vec2 texcoord) { return fuaHasLayer(" + std::to_string(kLayerNormal) + ") ? fuaNormalMapped(fuaWorldNormal(), fuaLayerTexel(" +
			std::to_string(kLayerNormal) + ", texcoord).xyz) : fuaWorldNormal(); }\n";
	b += "void SetMaterialProps(inout Material material, vec2 texCoord) {\n"
		"    material.Base = getTexel(texCoord.st);\n"
		"    material.Normal = ApplyNormalMap(texCoord.st);\n"
		"    material.Bright = fuaBrightTexel(texCoord.st);\n"
		"    material.Glow = fuaLayerTexel(" + std::to_string(kLayerGlow) + ", texCoord.st);\n";
	return b + "}\n";
}

// [Doruk-Aksoy] How an entry fills `material`, as UZDoom's func_defaultmat.fp, func_defaultmat2.fp or the shader's own SetupMaterial or ProcessMaterial does.
std::string FillMaterial(ProgramEntry entry, const std::string &n)
{
	switch (entry)
	{
	case kEntryTexCoord: return "    SetMaterialProps(material, fuaGetTexCoord_" + n + "());\n";
	case kEntrySetupMaterial: return "    fuaSetupMaterial_" + n + "(material);\n";
	case kEntryProcessMaterial: return "    material = fuaProcessMaterial_" + n + "();\n";
	default: break;
	}
	const std::string base = entry == kEntryTexel ? "fuaProcessTexel_" + n + "()" : "fuaProcess_" + n + "(vec4(1.0))";
	return "    material.Base = " + base + ";\n"
		"    material.Normal = ApplyNormalMap(vTexCoord.st);\n"
		"    material.Bright = fuaBrightTexel(vTexCoord.st);\n";
}

// [Doruk-Aksoy] What the program's light hook returns for `color`, handed to the shader's ProcessLight as hook says.
std::string LightCall(LightHook hook, const std::string &n)
{
	if (hook == kLightOld) return "fuaProcessLight_" + n + "(color)";
	if (hook == kLightMaterial) return "fuaProcessLight_" + n + "(fuaMat, color)";
	return "color";
}

} // namespace

const char *ComputeProgramMarker(ProgramKind kind)
{
	return NamesOf(kind).marker;
}

std::string ComputeUserProgramBlock(ProgramKind kind, const std::string &adapted, const ProgramSpec &spec, int index)
{
	static const UserShaderKey kNoKey;
	const ProgramNames &p = NamesOf(kind);
	const UserShaderKey &key = spec.key != nullptr ? *spec.key : kNoKey;
	const std::string n = std::to_string(index);
	std::string b = std::string(p.inputs) +
		"vec4 vTexCoord;\n"
		"float timer;\n"
		"Material fuaMat;\n" + MaterialHelpers(p, key.type) + Samplers(p, key);
	for (const auto &name : p.names) b += std::string("#define ") + name.first + " " + name.second + "\n";
	std::vector<ShaderDefine> defines = ComputeMaterialTypeDefines(key.type);
	defines.insert(defines.end(), key.defines.begin(), key.defines.end());
	b += ComputeDefineLines(defines) + ComputeUniformLines(key.uniforms) + ComputeDropUniformDeclarations(adapted, key.uniforms) +
		ComputeUnuniformLines(key.uniforms) + ComputeUndefineLines(defines) + "#undef texture2D\n#undef texture\n#undef textureSize\n#undef texelFetch\n";
	for (const auto &name : p.names) b += std::string("#undef ") + name.first + "\n";
	// [Doruk-Aksoy] UZDoom's getLightColor hands ProcessLight the light with the draw's alpha, and Zandronum 3.3's main.fp its ProcessLight the same.
	b += "vec4 fuaLight4(vec4 color) { return " + LightCall(spec.light, n) + "; }\n"
		"vec3 fuaUserLight(vec3 color) { return fuaLight4(vec4(color, " + p.lightAlpha + ")).rgb; }\n";
	const std::string start = "    vTexCoord = vec4(fuaUV, 0.0, 1.0);\n    timer = " + std::string(p.timer) + ";\n";
	if (spec.entry == kEntryProcessFinal)
	{
		b += std::string(p.entry) + p.guard + start + "    return " + ReplaceAll(p.finalCall, "@", "fuaProcess_" + n) + ";\n}\n";
		if (p.finalAlpha != nullptr) b += std::string("float fuaSurfaceAlpha() { return ") + p.finalAlpha + "; }\n";
		if (p.finalShade != nullptr) b += std::string("vec3 fuaSurfaceShade(vec4 t) { return ") + p.finalShade + "; }\n";
		return b;
	}
	b += "Material fuaMaterialOf(vec2 fuaUV) {\n" + start +
		"    Material material = Material(vec4(0.0), vec4(0.0), vec4(0.0), vec3(0.0), vec3(0.0), 0.0, 0.0, 0.0, 0.0, 0.0);\n" +
		FillMaterial(spec.entry, n) + "    return material;\n}\n";
	if (kind == k2DProgram)
		return b + p.entry + "    fuaMat = fuaMaterialOf(fuaUV);\n    return fuaMat.Base * fuaLight4(vColor);\n}\n";
	// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad main.fp: an old ProcessMaterial (LEGACY_USER_SHADER) takes no glow or bright layer.
	const char *layered = spec.entry == kEntryProcessMaterial ? "false" : "true";
	return b + p.entry + p.guard + "    fuaMat = fuaMaterialOf(fuaUV);\n    return fuaMat.Base;\n}\n"
		"float fuaSurfaceAlpha() { return vLightParm.z; }\n"
		"vec3 fuaSurfaceShade(vec4 t) { if (vWarp.x < 2.5) return fuaShade(t.rgb); fuaMat.Base = t; return fuaShadeMaterial(fuaMat, " +
		std::to_string((int)key.type) + ", " + layered + "); }\n";
}

std::string ComputePlainProgramBlock(ProgramKind kind)
{
	return NamesOf(kind).plain;
}

std::string ComputeSpliceShaderBlock(const std::string &source, const std::string &marker, const std::string &block)
{
	const size_t at = source.find(marker);
	if (at == std::string::npos) return source;
	const size_t lineStart = source.rfind('\n', at);
	const size_t from = lineStart == std::string::npos ? 0 : lineStart + 1;
	size_t to = source.find('\n', at);
	to = to == std::string::npos ? source.size() : to + 1;
	return source.substr(0, from) + block + source.substr(to);
}

std::string ComputePrecompileNote(int shaders, int programs, int leftOut, double ms)
{
	char buf[160];
	snprintf(buf, sizeof(buf), "vulkan: %d GLDEFS shader%s compiled into %d program%s in %.0f ms", shaders, shaders == 1 ? "" : "s",
		programs, programs == 1 ? "" : "s", ms);
	std::string note = buf;
	if (leftOut > 0) note += ", " + std::to_string(leftOut) + " left out";
	return note + "\n";
}

std::string ComputeIndentedLines(const std::string &text)
{
	std::string out;
	size_t from = 0;
	while (from < text.size())
	{
		size_t to = text.find('\n', from);
		if (to == std::string::npos) to = text.size();
		if (to > from) out += "  " + text.substr(from, to - from) + "\n";
		from = to + 1;
	}
	return out;
}

std::string ComputeUserShaderSkipNote(const std::string &renderer, const std::string &lump, const std::string &why)
{
	return renderer + ": hardware shader " + lump + " does not compile here; its textures draw without it\n" + ComputeIndentedLines(why);
}

}} // namespace zx::hwshaders
