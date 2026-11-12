// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLDEFS' custom hardware shaders for a renderer other than GL: a mod's GL fragment code adapted to sit beside the backend's own shader, and a piece's shader as the backend numbers it.

#ifndef ZX_HWSHADER_COMPUTE_H
#define ZX_HWSHADER_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace hwshaders {

// [rc4l] GL's FIRST_USER_SHADER: a texture's shaderindex from here up names a GLDEFS shader.
const int kFirstUserShader = 12;

// [rc4l] The shader a piece draws with, in the slot the warp style takes: an ANIMDEFS warp (1 or 2) first, as GL refuses a hardware shader on a warped texture, else 3 plus its GLDEFS shader (userShader, -1 for none), else 0.
int ComputePieceShader(int warp, int userShader);

// [Doruk-Aksoy] The GLDEFS shader a piece's shader slot (ComputePieceShader) names, or -1 for a warp or none.
int ComputeUserShaderOf(int pieceShader);

// [Doruk-Aksoy] The shader GL's 2D draw asks SetMaterial for, given the material's: -1 (the material's own) for a GLDEFS shader, which Zandronum 3.3 ran on a HUD image too, else 0, the default.
int ComputeDrawTextureShader(int materialShader);

// [Doruk-Aksoy] source with every whole-word `name[expr]`, its brackets matched, written as `func(expr)`.
std::string ComputeRewriteIndexedName(const std::string &source, const std::string &name, const std::string &func);

// [Doruk-Aksoy] Zandronum 3.3's names made to work on either renderer: loose declarations of camerapos and pixelpos dropped, camerapos read as uCameraPos.xyz unless the shader defines it, and gl_TextureMatrix[i] read through fuaTextureMatrix (7 the model matrix, 0 the texture matrix, else identity).
std::string ComputeLegacyShaderNames(const std::string &source);

// [Doruk-Aksoy] The entry points a GLDEFS shader's source defines, looked for by name as UZDoom's gl_shader.cpp does; materialLight is a ProcessLight taking UZDoom's Material first.
struct ShaderEntries { bool setupMaterial, processMaterial, getTexCoord, processTexel, processLight, materialLight; };
ShaderEntries ComputeShaderEntries(const std::string &source);

// [Doruk-Aksoy] How an old Process(color) shader is lit (GLDEFS fua_ProcessLighting): handed the light with its result final, as Zandronum 3.3 did, or handed white and lit after, as UZDoom does.
enum ProcessLighting { kLightingZandronum, kLightingUZDoom };

// [Doruk-Aksoy] The lighting a fua_ProcessLighting value names, in any case; false for an unknown one, leaving out alone.
bool ComputeProcessLightingOf(const std::string &value, ProcessLighting &out);

// [Doruk-Aksoy] How a program fills its material, in UZDoom's order: SetupMaterial, an old ProcessMaterial, GetTexCoord, ProcessTexel, Process handed white; or Zandronum 3.3's Process handed the light with its result final.
enum ProgramEntry { kEntryTexel, kEntryProcessLit, kEntryProcessFinal, kEntryTexCoord, kEntrySetupMaterial, kEntryProcessMaterial };
ProgramEntry ComputeProgramEntry(const ShaderEntries &entries, ProcessLighting lighting);

// [Doruk-Aksoy] How a program hands its shader's ProcessLight the light: no ProcessLight, UZDoom's old ProcessLight(color), or ProcessLight(Material, color).
enum LightHook { kLightNone, kLightOld, kLightMaterial };
LightHook ComputeLightHook(const ShaderEntries &entries);

// [Doruk-Aksoy] The line GL's main.fp is given ahead of it for an entry, empty for ProcessTexel.
const char *ComputeGLEntryDefine(ProgramEntry entry);

// [Doruk-Aksoy] UZDoom's shader types (GLDEFS `Material`), which pick the light model and how its textureN samplers are numbered.
enum MaterialType { kMaterialDefault, kMaterialSpecular, kMaterialPBR };

// [Doruk-Aksoy] The type a HardwareShader block's `Material` value names, in any case (UZDoom's normal, brightmap, specular, specularbrightmap, pbr, pbrbrightmap); false for an unknown one.
bool ComputeMaterialTypeOf(const std::string &value, MaterialType &out);

// [Doruk-Aksoy] A material's texture layers past its colour and brightmap, as the backend's layer rows hold them: UZDoom's maps, then the block's `Texture` lines from kLayerCustom.
enum MaterialLayer { kLayerNormal, kLayerSpecular, kLayerMetallic, kLayerRoughness, kLayerAO, kLayerDetail, kLayerGlow, kLayerCustom = 8, kLayerCount = 16 };

// [Doruk-Aksoy] How many `Texture` lines a type takes: what UZDoom's texture2..texture12 leave after its maps (8, 6 or 4).
int ComputeCustomTextureLimit(MaterialType type);

// [Doruk-Aksoy] What UZDoom's sampler textureN reads for a type (N from 2): a MaterialLayer, kSamplerBrightmap, or kSamplerNone past its last.
enum { kSamplerBrightmap = -1, kSamplerNone = -2, kSamplerBase = -3 };
int ComputeNumberedSampler(MaterialType type, int n);

// [Doruk-Aksoy] Why a `Texture <name>` line cannot be added to names, or null when it can: a name GLSL cannot take, one given twice, or more than limit.
const char *ComputeCustomTextureRefusal(const std::vector<std::string> &names, const std::string &name, int limit);

// [Doruk-Aksoy] UZDoom's glossiness and specular level in a row's spare entry, a byte each (glossiness to a whole number, level to hundredths) plus one, zero for one left at UZDoom's default.
unsigned ComputeSpecularWord(bool hasGlossiness, float glossiness, bool hasLevel, float level);

// [Doruk-Aksoy] One GLDEFS `Define NAME [= value]`, given ahead of the shader as UZDoom's `#define NAME value`.
struct ShaderDefine { std::string name, value; };

// [Doruk-Aksoy] The vec4s every texture shader's fua_Uniform values share after the frame's own constants, one per uniform.
enum { kTextureUniformSlots = 64 };

// [Doruk-Aksoy] One fua_Uniform a shader reads: its name, its components (1 to 4), whether it is an int, and its vec4 among the shared ones.
struct ShaderUniform
{
	std::string name;
	int components = 1;
	bool isInt = false;
	int slot = 0;
	bool perActor = false;   // [Doruk-Aksoy] read from the drawn actor's row (a fua_Material's), else from the vec4s texture shaders share
};

// [Doruk-Aksoy] One GLDEFS shader as the renderers compile it: its lump, how an old one is lit, its defines, its `Texture` names, its type and its fua_Uniform lines.
struct UserShaderKey
{
	std::string lump;
	ProcessLighting lighting = kLightingZandronum;
	std::vector<ShaderDefine> defines;
	std::vector<std::string> textures;
	MaterialType type = kMaterialDefault;
	std::string uniformSet;
	int actorMaterial = -1;   // [Doruk-Aksoy] the fua_Material this shader is, else -1
	std::vector<ShaderUniform> uniforms;
};

// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad gldefs.cpp ParseMaterial: a texture's light model from the maps it has, specular for normal and specular maps, PBR for normal, metallic, roughness and ambient occlusion ones, else the default.
MaterialType ComputeMaterialTypeFromLayers(bool normal, bool specular, bool metallic, bool roughness, bool ao);

// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad hw_material.cpp: the shader a texture draws with, its own (declared, -1 for none) only when its type is the one its maps give, else the built-in one for that type (builtin, -1 for none), and none for the default.
int ComputeMaterialShader(int declared, MaterialType declaredType, MaterialType inferred, int builtin);

// [Doruk-Aksoy] The key of the built-in shader a texture whose maps give `type` draws with when it has no fitting shader of its own: no lump, and that type.
UserShaderKey ComputeBuiltinMaterialKey(MaterialType type);

// [Doruk-Aksoy] What a built-in shader of `type` adapts as its source: UZDoom's default material for that type, every map read at the texture's own coordinate.
const char *ComputeBuiltinMaterialSource(MaterialType type);

// [Doruk-Aksoy] How notes and pipeline names call a shader: its lump, or what a built-in one is for.
std::string ComputeUserShaderName(const UserShaderKey &key);

// [Doruk-Aksoy] Whether two keys name one shader: the lump in any case and the rest as given.
bool ComputeSameUserShader(const UserShaderKey &a, const UserShaderKey &b);

// [Doruk-Aksoy] The index of the shader key names in shaders (ComputeSameUserShader), added at the end when it is new.
int ComputeUserShaderSlot(std::vector<UserShaderKey> &shaders, const UserShaderKey &key);

// [Doruk-Aksoy] The `#define` lines for a key's defines, in order, and the `#undef` lines that take them back.
std::string ComputeDefineLines(const std::vector<ShaderDefine> &defines);
std::string ComputeUndefineLines(const std::vector<ShaderDefine> &defines);

// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad hw_shaderpatcher.cpp defaultshaders: the defines a shader of each type is compiled with, SPECULAR or PBR and NORMALMAP.
std::vector<ShaderDefine> ComputeMaterialTypeDefines(MaterialType type);

// [Doruk-Aksoy] A uniform's GLSL type word: int, float or vec2 to vec4.
const char *ComputeUniformTypeName(const ShaderUniform &u);

// [Doruk-Aksoy] The `#define` lines naming each uniform's place among the shared vec4s, and the `#undef` lines that take them back.
std::string ComputeUniformLines(const std::vector<ShaderUniform> &uniforms);
std::string ComputeUnuniformLines(const std::vector<ShaderUniform> &uniforms);

// [Doruk-Aksoy] source without its own `uniform <type> <name>;` lines for the given uniforms, which the program declares.
std::string ComputeDropUniformDeclarations(const std::string &source, const std::vector<ShaderUniform> &uniforms);

// [Doruk-Aksoy] Why GL, kept only as a test fixture, leaves a shader out, or null when it runs it: GL runs the old entries and defines, not UZDoom's materials, texture coordinates or textures.
const char *ComputeGLRefusal(const UserShaderKey &key, ProgramEntry entry);

// [rc4l] A GLDEFS shader's GL source made to sit in the backend's: #version lines and the loose `uniform float timer;` and `uniform sampler2D tex;` dropped, legacy names adapted (ComputeLegacyShaderNames), gl_TexCoord[0] read as vTexCoord as GL's loader does, and its entry points renamed with `index`; returns them (ComputeShaderEntries).
ShaderEntries ComputeAdaptUserShader(const std::string &source, int index, std::string &out);

// [Doruk-Aksoy] The backend shaders a GLDEFS shader's program is made from: the world's scene stages, whose entry is fuaSurfaceTexel (with fuaSurfaceAlpha), and the 2D layer's, whose entry is fua2DColor.
enum ProgramKind { kSceneProgram, k2DProgram };

// [Doruk-Aksoy] The line a kind's backend shader marks its program block's place with.
const char *ComputeProgramMarker(ProgramKind kind);

// [Doruk-Aksoy] How one GLDEFS shader's program is put together around it: its entry and light hook, and its key's type, defines and texture names (none without a key).
struct ProgramSpec
{
	ProgramEntry entry = kEntryTexel;
	LightHook light = kLightNone;
	const UserShaderKey *key = nullptr;
};

// [Doruk-Aksoy] The program block of one GLDEFS shader, `adapted` by ComputeAdaptUserShader with `index`: GL main.fp's names for what the kind's shader holds under its own (pixelpos, uCameraPos, vColor, ModelMatrix and the rest), UZDoom's Material and samplers, the key's defines, the shader, and the kind's entry filling its material as spec says.
std::string ComputeUserProgramBlock(ProgramKind kind, const std::string &adapted, const ProgramSpec &spec, int index);

// [Doruk-Aksoy] The program block of a kind's shader without one: its entry samples the texture.
std::string ComputePlainProgramBlock(ProgramKind kind);

// [rc4l] source with the line holding `marker` replaced by block, or source as it is when it has no marker.
std::string ComputeSpliceShaderBlock(const std::string &source, const std::string &marker, const std::string &block);

// [Doruk-Aksoy] The console line the backend prints after compiling the GLDEFS shaders at launch: how many shaders, into how many programs, in how long, and how many were left out.
std::string ComputePrecompileNote(int shaders, int programs, int leftOut, double ms);

// [Doruk-Aksoy] text's non-blank lines, each indented two spaces, as a console note lists its reasons.
std::string ComputeIndentedLines(const std::string &text);

// [Doruk-Aksoy] The console note a renderer prints for a GLDEFS shader it leaves out, with `why`'s non-blank lines indented under it.
std::string ComputeUserShaderSkipNote(const std::string &renderer, const std::string &lump, const std::string &why);

}} // namespace zx::hwshaders

#endif // ZX_HWSHADER_COMPUTE_H
