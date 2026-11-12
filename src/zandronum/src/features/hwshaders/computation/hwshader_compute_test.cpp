// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwshaders/computation/hwshader_compute.h"

using namespace zx::hwshaders;

namespace {

// [Doruk-Aksoy] A key for a lump with the rest left as GLDEFS leaves it.
UserShaderKey KeyOf(const std::string &lump, ProcessLighting lighting = kLightingZandronum)
{
	UserShaderKey k;
	k.lump = lump;
	k.lighting = lighting;
	return k;
}

// [Doruk-Aksoy] A program spec for an entry and hook, keyed on k when given.
ProgramSpec SpecOf(ProgramEntry entry, LightHook light = kLightNone, const UserShaderKey *k = nullptr)
{
	ProgramSpec s;
	s.entry = entry;
	s.light = light;
	s.key = k;
	return s;
}

bool Has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

} // namespace

// [rc4l] A warp keeps its style and wins over a hardware shader; a GLDEFS shader is 3 up; nothing is 0.
TEST(HwShader, AWarpWinsAndAUserShaderIsThreeUp)
{
	EXPECT_EQ(1, ComputePieceShader(1, 4));
	EXPECT_EQ(2, ComputePieceShader(2, -1));
	EXPECT_EQ(3, ComputePieceShader(0, 0));
	EXPECT_EQ(7, ComputePieceShader(0, 4));
	EXPECT_EQ(0, ComputePieceShader(0, -1));
	EXPECT_EQ(0, ComputePieceShader(5, -1));   // [rc4l] not a warp the shaders know
}

// [Doruk-Aksoy] A piece's shader slot back to its GLDEFS shader, a warp or nothing being none.
TEST(HwShader, AShaderSlotNamesItsUserShader)
{
	EXPECT_EQ(-1, ComputeUserShaderOf(0));
	EXPECT_EQ(-1, ComputeUserShaderOf(1));
	EXPECT_EQ(-1, ComputeUserShaderOf(2));
	EXPECT_EQ(0, ComputeUserShaderOf(3));
	EXPECT_EQ(4, ComputeUserShaderOf(ComputePieceShader(0, 4)));
}

// [rc4l] func_warp1.fp's shape: the version, the loose timer and tex go, ProcessTexel is renamed, and the rest is left alone.
TEST(HwShader, ANewShaderLosesItsVersionAndLooseUniformsAndIsRenamed)
{
	const std::string src = "#version 120\nuniform float timer;\n  uniform   float timer ;\nuniform sampler2D tex;\nvec4 ProcessTexel()\n{\n\treturn getTexel(gl_TexCoord[0].st + timer);\n}";
	std::string out;
	ASSERT_TRUE(ComputeAdaptUserShader(src, 3, out).processTexel);
	EXPECT_FALSE(Has(out, "#version"));
	EXPECT_FALSE(Has(out, "uniform"));
	EXPECT_TRUE(Has(out, "vec4 fuaProcessTexel_3()"));
	EXPECT_TRUE(Has(out, "getTexel(vTexCoord.st + timer)"));
	EXPECT_EQ('\n', out.back());
}

// [rc4l] An old shader defines Process(color) and may define ProcessLight; each is renamed apart, and only whole words are touched.
TEST(HwShader, AnOldShaderKeepsProcessAndWholeWordsOnly)
{
	const std::string src = "vec4 Process(vec4 color) { return getTexel(vTexCoord.st) * color; }\nvec4 ProcessLight(vec4 c) { return c; }\nfloat MyProcess = 1.0; float Processes;\n";
	std::string out;
	const ShaderEntries e = ComputeAdaptUserShader(src, 0, out);
	EXPECT_FALSE(e.processTexel);
	EXPECT_TRUE(e.processLight);
	EXPECT_FALSE(e.materialLight);
	EXPECT_TRUE(Has(out, "vec4 fuaProcess_0(vec4 color)"));
	EXPECT_TRUE(Has(out, "vec4 fuaProcessLight_0(vec4 c)"));
	EXPECT_TRUE(Has(out, "MyProcess"));
	EXPECT_TRUE(Has(out, "Processes"));
}

// [Doruk-Aksoy] UZDoom's entry points are renamed with the shader's number like the old ones.
TEST(HwShader, UZDoomsEntryPointsAreRenamed)
{
	std::string out;
	const ShaderEntries e = ComputeAdaptUserShader("void SetupMaterial(inout Material m) {}\nMaterial ProcessMaterial() { Material m; return m; }\nvec2 GetTexCoord() { return vTexCoord.st; }\n", 6, out);
	EXPECT_TRUE(e.setupMaterial);
	EXPECT_TRUE(e.processMaterial);
	EXPECT_TRUE(e.getTexCoord);
	EXPECT_TRUE(Has(out, "void fuaSetupMaterial_6(inout Material m)"));
	EXPECT_TRUE(Has(out, "Material fuaProcessMaterial_6()"));
	EXPECT_TRUE(Has(out, "vec2 fuaGetTexCoord_6()"));
}

// [Doruk-Aksoy] An index is rewritten as a call with its brackets matched, nested ones included; a near name, a bare one or an unclosed one is left alone.
TEST(HwShader, AnIndexedNameBecomesACall)
{
	EXPECT_EQ("f(7)[3].xyz", ComputeRewriteIndexedName("m[7][3].xyz", "m", "f"));
	EXPECT_EQ("f(a[i + 1])", ComputeRewriteIndexedName("m [a[i + 1]]", "m", "f"));
	EXPECT_EQ("f(f(0))", ComputeRewriteIndexedName("m[m[0]]", "m", "f"));
	EXPECT_EQ("mm[1] m2[1] m; m[", ComputeRewriteIndexedName("mm[1] m2[1] m; m[", "m", "f"));
	EXPECT_EQ("x m", ComputeRewriteIndexedName("x m", "m", "f"));
	EXPECT_EQ("plain", ComputeRewriteIndexedName("plain", "m", "f"));
}

// [Doruk-Aksoy] DnD's Fireball.fp shape: 3.3's declarations go, gl_TextureMatrix reads through fuaTextureMatrix, and camerapos is defined for it.
TEST(HwShader, ZandronumThreeThreeNamesAreAdapted)
{
	const std::string src = "uniform vec3 camerapos;\n varying  vec4 pixelpos ;\nin vec4 pixelpos;\nmat4 m = gl_TextureMatrix[7];\nvec3 we = camerapos - m[3].xyz;\n";
	EXPECT_EQ("#define camerapos (uCameraPos.xyz)\n"
		"mat4 fuaTextureMatrix(int i) { return i == 7 ? ModelMatrix : (i == 0 ? TextureMatrix : mat4(1.0)); }\n"
		"mat4 m = fuaTextureMatrix(7);\nvec3 we = camerapos - m[3].xyz;\n", ComputeLegacyShaderNames(src));
	// [Doruk-Aksoy] Nothing legacy, nothing added.
	EXPECT_EQ("vec4 Process(vec4 c) { return c; }\n", ComputeLegacyShaderNames("vec4 Process(vec4 c) { return c; }"));
}

// [Doruk-Aksoy] A shader that defines camerapos or declares it a vector keeps its own, as DnD's __VERSION__ fix and a ported shader do.
TEST(HwShader, AShadersOwnCameraposIsKept)
{
	EXPECT_FALSE(Has(ComputeLegacyShaderNames("#define camerapos uCameraPos.xyz\nvec3 a = camerapos;\n"), "(uCameraPos.xyz)"));
	EXPECT_FALSE(Has(ComputeLegacyShaderNames("  #  define camerapos uCameraPos.xyz\n"), "(uCameraPos.xyz)"));
	EXPECT_FALSE(Has(ComputeLegacyShaderNames("vec3 camerapos = uCameraPos.xyz;\n"), "(uCameraPos.xyz)"));
	EXPECT_FALSE(Has(ComputeLegacyShaderNames("float\tcamerapos;\n"), "(uCameraPos.xyz)"));
	// [Doruk-Aksoy] Only a whole word counts, as a use or as a definition.
	EXPECT_FALSE(Has(ComputeLegacyShaderNames("float mycamerapos;\n"), "#define camerapos"));
	EXPECT_TRUE(Has(ComputeLegacyShaderNames("undefine camerapos;\nx = camerapos;\n"), "#define camerapos"));
	EXPECT_TRUE(Has(ComputeLegacyShaderNames("define camerapos;\n"), "#define camerapos"));
	EXPECT_TRUE(Has(ComputeLegacyShaderNames("vec3 mycamerapos = camerapos;\n"), "#define camerapos"));
}

// [Doruk-Aksoy] The backend's adaptation takes the legacy names too, ahead of its own renames.
TEST(HwShader, TheBackendsAdaptationTakesLegacyNames)
{
	std::string out;
	const ShaderEntries e = ComputeAdaptUserShader("uniform vec3 camerapos;\nvec4 Process(vec4 c) { return vec4(gl_TextureMatrix[7][3].xyz - camerapos, 1.0); }\n", 2, out);
	EXPECT_FALSE(e.processTexel || e.setupMaterial || e.processMaterial || e.getTexCoord || e.processLight);
	EXPECT_EQ(0u, out.find("#define camerapos (uCameraPos.xyz)\nmat4 fuaTextureMatrix(int i)"));
	EXPECT_TRUE(Has(out, "vec4 fuaProcess_2(vec4 c) { return vec4(fuaTextureMatrix(7)[3].xyz - camerapos, 1.0); }"));
}

// [Doruk-Aksoy] A shader's own program: GL's names over the scene shader's, UZDoom's helpers and samplers, the shader, and its material filled by its entry, then shaded through fuaShadeMaterial.
TEST(HwShader, AProgramBlockGivesGLsNamesAndFillsItsMaterial)
{
	const std::string b = ComputeUserProgramBlock(kSceneProgram, "A\n", SpecOf(kEntryTexel), 4);
	EXPECT_TRUE(Has(b, "vec4 getTexel(vec2 st) { return fuaTexMode(fuaTexel(st)); }\n"));
	EXPECT_TRUE(Has(b, "vec4 desaturate(vec4 texel) { return vec4(fuaDesaturate(texel.rgb), texel.a); }\n"));
	EXPECT_TRUE(Has(b, "vec4 fuaSample_tex(vec2 uv) { return fuaTexel(uv); }\n"));
	EXPECT_TRUE(Has(b, "#define texture2D(s, uv) fuaSample_##s(uv)\n"));
	EXPECT_TRUE(Has(b, "#define texture(s, uv) fuaSample_##s(uv)\n"));
	EXPECT_TRUE(Has(b, "#define pixelpos vPixelPos\n"));
	EXPECT_TRUE(Has(b, "#define glowdist vGlowDist\n"));
	EXPECT_TRUE(Has(b, "#define uCameraPos vec4(uCameraPos.xyz, 0.0)\n"));
	EXPECT_TRUE(Has(b, "#define vColor vec4(vColor, vLightParm.z)\n"));
	EXPECT_TRUE(Has(b, "#define uDesaturationFactor vDesaturation\n"));
	EXPECT_TRUE(Has(b, "#define ModelMatrix mat4(vModel0, vModel1, vModel2, vModel3)\n"));
	EXPECT_TRUE(Has(b, "layout(location = 23) flat in vec4 vModel0;\n"));
	EXPECT_TRUE(Has(b, "layout(location = 26) flat in vec4 vModel3;\n"));
	EXPECT_TRUE(Has(b, "#define uSpecularMaterial fuaSpecularMaterial()\n#define vWorldNormal vec4(fuaWorldNormal(), 0.0)\nA\n#undef texture2D\n"));
	EXPECT_TRUE(Has(b, "    if (vWarp.x < 2.5) return fuaTexMode(fuaTexel(fuaUV));\n"));
	EXPECT_TRUE(Has(b, "    timer = uFrame.x * vWarp.y;\n"));
	EXPECT_TRUE(Has(b, "    material.Base = fuaProcessTexel_4();\n    material.Normal = ApplyNormalMap(vTexCoord.st);\n    material.Bright = fuaBrightTexel(vTexCoord.st);\n"));
	EXPECT_TRUE(Has(b, "    fuaMat = fuaMaterialOf(fuaUV);\n    return fuaMat.Base;\n"));
	EXPECT_TRUE(Has(b, "float fuaSurfaceAlpha() { return vLightParm.z; }\n"));
	EXPECT_TRUE(Has(b, "vec3 fuaSurfaceShade(vec4 t) { if (vWarp.x < 2.5) return fuaShade(t.rgb); fuaMat.Base = t; return fuaShadeMaterial(fuaMat, 0, true); }\n"));
	// [Doruk-Aksoy] Without a ProcessLight the hook hands the light back as it came.
	EXPECT_TRUE(Has(b, "vec4 fuaLight4(vec4 color) { return color; }\n"));
	EXPECT_TRUE(Has(b, "vec3 fuaUserLight(vec3 color) { return fuaLight4(vec4(color, vLightParm.z)).rgb; }\n"));
	// [Doruk-Aksoy] Every name the block defines is undefined again before its own code.
	for (const char *name : { "texture2D", "texture", "pixelpos", "glowdist", "uCameraPos", "vColor", "uDesaturationFactor", "ModelMatrix", "TextureMatrix" })
		EXPECT_TRUE(Has(b, std::string("#undef ") + name + "\n")) << name;
}

// [Doruk-Aksoy] Zandronum 3.3's Process is handed the light with the piece's alpha and only finished after, keeping its own alpha, while a warped piece stays plain.
TEST(HwShader, AZandronumProcessIsHandedTheLightAndIsFinal)
{
	const std::string old = ComputeUserProgramBlock(kSceneProgram, "B\n", SpecOf(kEntryProcessFinal, kLightOld), 0);
	EXPECT_TRUE(Has(old, "    if (vWarp.x < 2.5) return fuaTexMode(fuaTexel(fuaUV));\n"));
	EXPECT_TRUE(Has(old, "    return fuaProcess_0(vec4(fuaProcessLight(), vLightParm.z));\n"));
	EXPECT_TRUE(Has(old, "float fuaSurfaceAlpha() { return vWarp.x < 2.5 ? vLightParm.z : 1.0; }\n"));
	EXPECT_TRUE(Has(old, "vec3 fuaSurfaceShade(vec4 t) { return vWarp.x < 2.5 ? fuaShade(t.rgb) : fuaShadeFinal(t.rgb); }\n"));
	// [Doruk-Aksoy] Its ProcessLight is handed the light, as 3.3's main.fp called it before the dynamic lights.
	EXPECT_TRUE(Has(old, "vec4 fuaLight4(vec4 color) { return fuaProcessLight_0(color); }\n"));
	EXPECT_FALSE(Has(old, "fuaProcessTexel_"));
	EXPECT_FALSE(Has(old, "fuaMaterialOf"));
}

// [Doruk-Aksoy] UZDoom's Process is handed white and lit after like a texel, its alpha the piece's.
TEST(HwShader, AUZDoomProcessIsHandedWhiteAndLitAfter)
{
	const std::string lit = ComputeUserProgramBlock(kSceneProgram, "B\n", SpecOf(kEntryProcessLit), 5);
	EXPECT_TRUE(Has(lit, "    material.Base = fuaProcess_5(vec4(1.0));\n"));
	EXPECT_TRUE(Has(lit, "float fuaSurfaceAlpha() { return vLightParm.z; }\n"));
	EXPECT_TRUE(Has(lit, "return fuaShadeMaterial(fuaMat, 0, true); }\n"));
	EXPECT_FALSE(Has(lit, "fuaProcessLight()"));
}

// [Doruk-Aksoy] UZDoom's material entries: GetTexCoord feeds SetMaterialProps, SetupMaterial fills it, and an old ProcessMaterial returns it and takes no glow or bright layer.
TEST(HwShader, UZDoomsMaterialEntriesFillTheMaterial)
{
	EXPECT_TRUE(Has(ComputeUserProgramBlock(kSceneProgram, "C\n", SpecOf(kEntryTexCoord), 1), "    SetMaterialProps(material, fuaGetTexCoord_1());\n"));
	const std::string setup = ComputeUserProgramBlock(kSceneProgram, "C\n", SpecOf(kEntrySetupMaterial, kLightMaterial), 2);
	EXPECT_TRUE(Has(setup, "    fuaSetupMaterial_2(material);\n"));
	EXPECT_TRUE(Has(setup, "vec4 fuaLight4(vec4 color) { return fuaProcessLight_2(fuaMat, color); }\n"));
	EXPECT_TRUE(Has(setup, "fuaShadeMaterial(fuaMat, 0, true)"));
	const std::string legacy = ComputeUserProgramBlock(kSceneProgram, "C\n", SpecOf(kEntryProcessMaterial), 3);
	EXPECT_TRUE(Has(legacy, "    material = fuaProcessMaterial_3();\n"));
	EXPECT_TRUE(Has(legacy, "fuaShadeMaterial(fuaMat, 0, false)"));
	// [Doruk-Aksoy] Every material starts zeroed, as UZDoom's main() clears it.
	EXPECT_TRUE(Has(setup, "    Material material = Material(vec4(0.0), vec4(0.0), vec4(0.0), vec3(0.0), vec3(0.0), 0.0, 0.0, 0.0, 0.0, 0.0);\n"));
}

// [Doruk-Aksoy] SetMaterialProps reads colour, normal, bright and glow as UZDoom's does, the normal map only where the type has one, and never the specular or PBR maps, which a type's shader reads itself.
TEST(HwShader, SetMaterialPropsReadsWhatUZDoomsDoes)
{
	UserShaderKey k = KeyOf("a.fp");
	const std::string plain = ComputeUserProgramBlock(kSceneProgram, "", SpecOf(kEntryTexCoord, kLightNone, &k), 0);
	EXPECT_TRUE(Has(plain, "vec3 ApplyNormalMap(vec2 texcoord) { return fuaWorldNormal(); }\n"));
	EXPECT_TRUE(Has(plain, "    material.Glow = fuaLayerTexel(6, texCoord.st);\n"));
	for (MaterialType t : { kMaterialSpecular, kMaterialPBR })
	{
		k.type = t;
		const std::string b = ComputeUserProgramBlock(kSceneProgram, "", SpecOf(kEntryTexCoord, kLightNone, &k), 0);
		EXPECT_TRUE(Has(b, "vec3 ApplyNormalMap(vec2 texcoord) { return fuaHasLayer(0) ? fuaNormalMapped(fuaWorldNormal(), fuaLayerTexel(0, texcoord).xyz) : fuaWorldNormal(); }\n"));
		EXPECT_FALSE(Has(b, "material.Specular =") || Has(b, "material.Metallic ="));
		EXPECT_TRUE(Has(b, std::string("fuaShadeMaterial(fuaMat, ") + (t == kMaterialSpecular ? "1" : "2") + ", true)"));
	}
}

// [Doruk-Aksoy] A type's built-in shader fills what UZDoom's func_spec.fp and func_pbr.fp do, through the names they read, and the default only SetMaterialProps.
TEST(HwShader, ABuiltInShaderReadsItsTypesMaps)
{
	const std::string spec = ComputeBuiltinMaterialSource(kMaterialSpecular);
	EXPECT_TRUE(Has(spec, "material.Specular = texture(speculartexture, vTexCoord.st).rgb;"));
	EXPECT_TRUE(Has(spec, "material.Glossiness = uSpecularMaterial.x;"));
	EXPECT_TRUE(Has(spec, "material.SpecularLevel = uSpecularMaterial.y;"));
	const std::string pbr = ComputeBuiltinMaterialSource(kMaterialPBR);
	EXPECT_TRUE(Has(pbr, "material.Metallic = texture(metallictexture, vTexCoord.st).r;"));
	EXPECT_TRUE(Has(pbr, "material.Roughness = texture(roughnesstexture, vTexCoord.st).r;"));
	EXPECT_TRUE(Has(pbr, "material.AO = texture(aotexture, vTexCoord.st).r;"));
	EXPECT_EQ("void SetupMaterial(inout Material material)\n{\n\tSetMaterialProps(material, vTexCoord.st);\n}\n", std::string(ComputeBuiltinMaterialSource(kMaterialDefault)));
	UserShaderKey k = ComputeBuiltinMaterialKey(kMaterialSpecular);
	const std::string b = ComputeUserProgramBlock(kSceneProgram, spec, SpecOf(kEntrySetupMaterial, kLightNone, &k), 0);
	EXPECT_TRUE(Has(b, "#define uSpecularMaterial fuaSpecularMaterial()\n"));
	EXPECT_TRUE(Has(ComputeUserProgramBlock(k2DProgram, spec, SpecOf(kEntrySetupMaterial, kLightNone, &k), 0), "#define uSpecularMaterial fuaSpecularMaterial()\n"));
}

// [Doruk-Aksoy] Each Texture line and textureN up to the last custom one gets a sampler, and the key's defines wrap the shader alone.
TEST(HwShader, AKeysTexturesAndDefinesReachItsProgram)
{
	UserShaderKey k = KeyOf("a.fp");
	k.textures = { "noisetex", "masktex" };
	k.defines = { { "WAVES", "3" }, { "SOFT", "" } };
	const std::string b = ComputeUserProgramBlock(kSceneProgram, "S\n", SpecOf(kEntryTexel, kLightNone, &k), 0);
	EXPECT_TRUE(Has(b, "vec4 fuaSample_noisetex(vec2 uv) { return fuaLayerTexel(8, uv); }\n"));
	EXPECT_TRUE(Has(b, "vec4 fuaSample_masktex(vec2 uv) { return fuaLayerTexel(9, uv); }\n"));
	EXPECT_TRUE(Has(b, "vec4 fuaSample_texture2(vec2 uv) { return fuaBrightTexel(uv); }\n"));
	EXPECT_TRUE(Has(b, "vec4 fuaSample_texture5(vec2 uv) { return fuaLayerTexel(8, uv); }\n"));
	EXPECT_TRUE(Has(b, "vec4 fuaSample_texture6(vec2 uv) { return fuaLayerTexel(9, uv); }\n"));
	EXPECT_FALSE(Has(b, "fuaSample_texture7("));
	EXPECT_TRUE(Has(b, "vec4 fuaSample_normaltexture(vec2 uv) { return fuaLayerTexel(0, uv); }\n"));
	EXPECT_TRUE(Has(b, "#define WAVES 3\n#define SOFT\nS\n#undef WAVES\n#undef SOFT\n"));
}

// [Doruk-Aksoy] A typed shader is compiled with UZDoom's type defines ahead of its own, and both are taken back after it.
TEST(HwShader, ATypedShaderGetsUZDoomsDefines)
{
	EXPECT_TRUE(ComputeMaterialTypeDefines(kMaterialDefault).empty());
	UserShaderKey k = KeyOf("a.fp");
	k.type = kMaterialSpecular;
	k.defines = { { "WAVES", "3" } };
	EXPECT_TRUE(Has(ComputeUserProgramBlock(kSceneProgram, "S\n", SpecOf(kEntryTexel, kLightNone, &k), 0),
		"#define SPECULAR\n#define NORMALMAP\n#define WAVES 3\nS\n#undef SPECULAR\n#undef NORMALMAP\n#undef WAVES\n"));
	k.type = kMaterialPBR;
	EXPECT_TRUE(Has(ComputeUserProgramBlock(kSceneProgram, "S\n", SpecOf(kEntryTexel, kLightNone, &k), 0), "#define PBR\n#define NORMALMAP\n#define WAVES 3\n"));
}

// [Doruk-Aksoy] textureSize and texelFetch reach every sampler name as texture does, tex the surface's own texture, and all four are taken back after the shader.
TEST(HwShader, EverySamplerHasASizeAndATexel)
{
	UserShaderKey k = KeyOf("a.fp");
	k.textures = { "noisetex" };
	const std::string b = ComputeUserProgramBlock(kSceneProgram, "S\n", SpecOf(kEntryTexel, kLightNone, &k), 0);
	EXPECT_TRUE(Has(b, "ivec2 fuaSize_tex(int lod) { return fuaLayerSize(-3, lod); }\n"));
	EXPECT_TRUE(Has(b, "vec4 fuaFetch_tex(ivec2 p, int lod) { return fuaLayerFetch(-3, p, lod); }\n"));
	EXPECT_TRUE(Has(b, "ivec2 fuaSize_noisetex(int lod) { return fuaLayerSize(8, lod); }\n"));
	EXPECT_TRUE(Has(b, "vec4 fuaFetch_brighttexture(ivec2 p, int lod) { return fuaLayerFetch(-1, p, lod); }\n"));
	EXPECT_TRUE(Has(b, "#define textureSize(s, lod) fuaSize_##s(lod)\n#define texelFetch(s, p, lod) fuaFetch_##s(p, lod)\n"));
	EXPECT_TRUE(Has(b, "#undef texture2D\n#undef texture\n#undef textureSize\n#undef texelFetch\n"));
}

// [Doruk-Aksoy] A 2D program reads the 2D layer's texel and colour, has no world to place, and lights its material with the draw's colour as UZDoom's 2D path does.
TEST(HwShader, A2DProgramBlockTakesThe2DLayersNames)
{
	const std::string b = ComputeUserProgramBlock(k2DProgram, "C\n", SpecOf(kEntryTexel), 2);
	EXPECT_FALSE(Has(b, "vModel0"));
	EXPECT_TRUE(Has(b, "vec4 getTexel(vec2 st) { return fua2DTexel(st); }\n"));
	EXPECT_TRUE(Has(b, "vec4 fuaSample_tex(vec2 uv) { return fua2DRaw(uv); }\n"));
	EXPECT_TRUE(Has(b, "vec4 desaturate(vec4 texel) { return texel; }\n"));
	EXPECT_TRUE(Has(b, "#define pixelpos vec4(gl_FragCoord.xy, 0.0, 0.0)\n"));
	EXPECT_TRUE(Has(b, "#define ModelMatrix mat4(1.0)\n"));
	EXPECT_FALSE(Has(b, "#define vColor"));
	EXPECT_TRUE(Has(b, "vec4 fua2DColor(vec2 fuaUV) {\n    fuaMat = fuaMaterialOf(fuaUV);\n    return fuaMat.Base * fuaLight4(vColor);\n}\n"));
	EXPECT_TRUE(Has(b, "    timer = uInvScreen.z * vSpeed;\n"));
	EXPECT_TRUE(Has(b, "vec3 fuaUserLight(vec3 color) { return fuaLight4(vec4(color, vColor.a)).rgb; }\n"));
	EXPECT_FALSE(Has(b, "fuaSurfaceAlpha"));
	EXPECT_FALSE(Has(b, "fuaSurfaceShade"));
	// [Doruk-Aksoy] An old 2D entry is handed the draw's colour as 3.3 handed it, or white with the colour multiplied after as UZDoom would light it.
	EXPECT_TRUE(Has(ComputeUserProgramBlock(k2DProgram, "D\n", SpecOf(kEntryProcessFinal), 1), "    return fuaProcess_1(vColor);\n"));
	EXPECT_TRUE(Has(ComputeUserProgramBlock(k2DProgram, "D\n", SpecOf(kEntryProcessLit), 1), "    material.Base = fuaProcess_1(vec4(1.0));\n"));
}

TEST(HwShader, APlainBlockSamplesTheTexture)
{
	EXPECT_EQ("vec4 fuaSurfaceTexel(vec2 fuaUV) { return fuaTexMode(fuaTexel(fuaUV)); }\nfloat fuaSurfaceAlpha() { return vLightParm.z; }\n"
		"vec3 fuaSurfaceShade(vec4 t) { return fuaShade(t.rgb); }\n",
		ComputePlainProgramBlock(kSceneProgram));
	EXPECT_EQ("vec4 fua2DColor(vec2 fuaUV) { return fua2DTexel(fuaUV) * vColor; }\n", ComputePlainProgramBlock(k2DProgram));
}

// [Doruk-Aksoy] Each kind marks its block's place apart, and neither marker is found inside the other.
TEST(HwShader, EachKindHasItsOwnMarker)
{
	const std::string scene = ComputeProgramMarker(kSceneProgram), twoD = ComputeProgramMarker(k2DProgram);
	EXPECT_EQ("//FUA_USER_SHADERS", scene);
	EXPECT_EQ("//FUA_2D_SHADERS", twoD);
	EXPECT_FALSE(Has(scene, twoD));
	EXPECT_FALSE(Has(twoD, scene));
}

// [Doruk-Aksoy] GL's 2D draw takes a GLDEFS shader's material shader and the default for anything else, a warp or a brightmap included.
TEST(HwShader, A2DDrawTakesOnlyAUserShader)
{
	EXPECT_EQ(-1, ComputeDrawTextureShader(kFirstUserShader));
	EXPECT_EQ(-1, ComputeDrawTextureShader(kFirstUserShader + 5));
	EXPECT_EQ(0, ComputeDrawTextureShader(0));
	EXPECT_EQ(0, ComputeDrawTextureShader(3));
	EXPECT_EQ(0, ComputeDrawTextureShader(kFirstUserShader - 1));
}

TEST(HwShader, TheBlockReplacesTheMarkersLine)
{
	EXPECT_EQ("a\nBLOCK\nc\n", ComputeSpliceShaderBlock("a\n  //MARK x\nc\n", "//MARK", "BLOCK\n"));
	EXPECT_EQ("BLOCK\nc", ComputeSpliceShaderBlock("//MARK\nc", "//MARK", "BLOCK\n"));
	EXPECT_EQ("a\nBLOCK\n", ComputeSpliceShaderBlock("a\n//MARK", "//MARK", "BLOCK\n"));
	EXPECT_EQ("plain", ComputeSpliceShaderBlock("plain", "//MARK", "BLOCK\n"));
}

// [Doruk-Aksoy] The launch note counts shaders and programs, singular for one, and names what was left out only when something was.
TEST(HwShader, ThePrecompileNoteCountsWhatCompiled)
{
	EXPECT_EQ("vulkan: 38 GLDEFS shaders compiled into 152 programs in 1234 ms\n", ComputePrecompileNote(38, 152, 0, 1234.4));
	EXPECT_EQ("vulkan: 1 GLDEFS shader compiled into 1 program in 5 ms, 2 left out\n", ComputePrecompileNote(1, 1, 2, 4.6));
}

// [Doruk-Aksoy] Both renderers note a shader they leave out alike, GL with the compiler's complaint under it, indented and ending in a newline.
TEST(HwShader, ASkippedShaderIsNotedByRendererAndLump)
{
	EXPECT_EQ("vulkan: hardware shader shaders/a.fp does not compile here; its textures draw without it\n",
		ComputeUserShaderSkipNote("vulkan", "shaders/a.fp", ""));
	EXPECT_EQ("GL: hardware shader shaders/a.fp does not compile here; its textures draw without it\n"
		"  Fragment shader:\n  0(3) : error: undefined variable \"camerapos\"\n",
		ComputeUserShaderSkipNote("GL", "shaders/a.fp", "Fragment shader:\n0(3) : error: undefined variable \"camerapos\"\n\n"));
	EXPECT_EQ("GL: hardware shader b.fp does not compile here; its textures draw without it\n  no newline\n",
		ComputeUserShaderSkipNote("GL", "b.fp", "no newline"));
}

// [Doruk-Aksoy] Each entry point is found by whole name, and a ProcessLight taking a Material first is UZDoom's new one.
TEST(HwShader, AShadersEntryPointsAreFoundByName)
{
	const ShaderEntries none = ComputeShaderEntries("vec4 Process(vec4 color) { return color; }");
	EXPECT_FALSE(none.setupMaterial || none.processMaterial || none.getTexCoord || none.processTexel || none.processLight || none.materialLight);
	EXPECT_TRUE(ComputeShaderEntries("vec4 ProcessTexel() { return vec4(1.0); }").processTexel);
	EXPECT_FALSE(ComputeShaderEntries("vec4 MyProcessTexel() { return vec4(1.0); }").processTexel);
	const ShaderEntries newLight = ComputeShaderEntries("vec4 ProcessLight (\n  Material material, vec4 color) { return color; }");
	EXPECT_TRUE(newLight.processLight);
	EXPECT_TRUE(newLight.materialLight);
	const ShaderEntries oldLight = ComputeShaderEntries("vec4 ProcessLight(vec4 color) { return color; } Material x; vec4 ProcessLight; vec4 ProcessLight(MaterialX m);");
	EXPECT_TRUE(oldLight.processLight);
	EXPECT_FALSE(oldLight.materialLight);
}

// [Doruk-Aksoy] UZDoom's order: SetupMaterial, ProcessMaterial, GetTexCoord, ProcessTexel, then Process by its lighting.
TEST(HwShader, AnEntryIsPickedInUZDoomsOrder)
{
	ShaderEntries e = {};
	EXPECT_EQ(kEntryProcessFinal, ComputeProgramEntry(e, kLightingZandronum));
	EXPECT_EQ(kEntryProcessLit, ComputeProgramEntry(e, kLightingUZDoom));
	e.processTexel = true;
	EXPECT_EQ(kEntryTexel, ComputeProgramEntry(e, kLightingZandronum));
	EXPECT_EQ(kEntryTexel, ComputeProgramEntry(e, kLightingUZDoom));
	e.getTexCoord = true;
	EXPECT_EQ(kEntryTexCoord, ComputeProgramEntry(e, kLightingZandronum));
	e.processMaterial = true;
	EXPECT_EQ(kEntryProcessMaterial, ComputeProgramEntry(e, kLightingZandronum));
	e.setupMaterial = true;
	EXPECT_EQ(kEntrySetupMaterial, ComputeProgramEntry(e, kLightingUZDoom));
}

// [Doruk-Aksoy] A light hook only for a ProcessLight, UZDoom's Material one apart from the old.
TEST(HwShader, ALightHookFollowsProcessLight)
{
	ShaderEntries e = {};
	EXPECT_EQ(kLightNone, ComputeLightHook(e));
	e.processLight = true;
	EXPECT_EQ(kLightOld, ComputeLightHook(e));
	e.materialLight = true;
	EXPECT_EQ(kLightMaterial, ComputeLightHook(e));
}

// [Doruk-Aksoy] fua_ProcessLighting takes either name in any case, and an unknown one leaves the lighting as it was.
TEST(HwShader, AProcessLightingValueIsReadInAnyCase)
{
	ProcessLighting l = kLightingZandronum;
	EXPECT_TRUE(ComputeProcessLightingOf("UZDoom", l));
	EXPECT_EQ(kLightingUZDoom, l);
	EXPECT_TRUE(ComputeProcessLightingOf("zandronum", l));
	EXPECT_EQ(kLightingZandronum, l);
	l = kLightingUZDoom;
	EXPECT_FALSE(ComputeProcessLightingOf("gzdoom", l));
	EXPECT_FALSE(ComputeProcessLightingOf("", l));
	EXPECT_EQ(kLightingUZDoom, l);
}

// [Doruk-Aksoy] GL's main.fp is told an old entry's lighting by a define, and nothing for the rest.
TEST(HwShader, GLIsToldAnOldEntrysLightingByADefine)
{
	EXPECT_STREQ("", ComputeGLEntryDefine(kEntryTexel));
	EXPECT_STREQ("#define FUA_PROCESS_UZDOOM\n", ComputeGLEntryDefine(kEntryProcessLit));
	EXPECT_STREQ("#define FUA_PROCESS_ZANDRONUM\n", ComputeGLEntryDefine(kEntryProcessFinal));
	EXPECT_STREQ("", ComputeGLEntryDefine(kEntrySetupMaterial));
}

// [Doruk-Aksoy] GL, a test fixture now, runs the old entries and defines but refuses UZDoom's materials, texture coordinates, textures and types.
TEST(HwShader, GLRefusesWhatOnlyVulkanRuns)
{
	const UserShaderKey plain = KeyOf("a.fp");
	for (ProgramEntry e : { kEntryTexel, kEntryProcessLit, kEntryProcessFinal }) EXPECT_EQ(nullptr, ComputeGLRefusal(plain, e)) << e;
	for (ProgramEntry e : { kEntryTexCoord, kEntrySetupMaterial, kEntryProcessMaterial }) EXPECT_NE(nullptr, ComputeGLRefusal(plain, e)) << e;
	UserShaderKey textured = plain;
	textured.textures = { "noise" };
	EXPECT_NE(nullptr, ComputeGLRefusal(textured, kEntryTexel));
	UserShaderKey typed = plain;
	typed.type = kMaterialPBR;
	EXPECT_NE(nullptr, ComputeGLRefusal(typed, kEntryTexel));
	UserShaderKey defined = plain;
	defined.defines = { { "A", "1" } };
	EXPECT_EQ(nullptr, ComputeGLRefusal(defined, kEntryTexel));
	UserShaderKey uniformed = plain;
	uniformed.uniforms.push_back(ShaderUniform());
	EXPECT_NE(nullptr, ComputeGLRefusal(uniformed, kEntryTexel));
}

namespace {

// [Doruk-Aksoy] A uniform as a set gives it.
ShaderUniform UniformOf(const char *name, int components, bool isInt, int slot)
{
	ShaderUniform u;
	u.name = name;
	u.components = components;
	u.isInt = isInt;
	u.slot = slot;
	return u;
}

} // namespace

// [Doruk-Aksoy] Each uniform names its shared vec4 by its components, an int read from its bits, and is taken back after the shader.
TEST(HwShader, AUniformNamesItsSharedVec4)
{
	const std::vector<ShaderUniform> u = { UniformOf("glow", 1, false, 0), UniformOf("dir", 2, false, 1), UniformOf("tint", 3, false, 7),
		UniformOf("col", 4, false, 63), UniformOf("mode", 1, true, 2), UniformOf("odd", 9, false, 3) };
	EXPECT_EQ("#define glow (uFuaTexUniforms[0].x)\n#define dir (uFuaTexUniforms[1].xy)\n#define tint (uFuaTexUniforms[7].xyz)\n"
		"#define col (uFuaTexUniforms[63])\n#define mode floatBitsToInt(uFuaTexUniforms[2].x)\n#define odd (uFuaTexUniforms[3])\n", ComputeUniformLines(u));
	EXPECT_EQ("#undef glow\n#undef dir\n#undef tint\n#undef col\n#undef mode\n#undef odd\n", ComputeUnuniformLines(u));
	EXPECT_EQ("", ComputeUniformLines({}));
	EXPECT_STREQ("float", ComputeUniformTypeName(u[0]));
	EXPECT_STREQ("vec2", ComputeUniformTypeName(u[1]));
	EXPECT_STREQ("vec3", ComputeUniformTypeName(u[2]));
	EXPECT_STREQ("vec4", ComputeUniformTypeName(u[3]));
	EXPECT_STREQ("int", ComputeUniformTypeName(u[4]));
	EXPECT_STREQ("float", ComputeUniformTypeName(UniformOf("z", 0, false, 0)));
}

// [Doruk-Aksoy] An actor's uniform reads its row, an int as a float, and a fua_Material's key is its own, refused on GL.
TEST(HwShader, AnActorsUniformReadsItsRow)
{
	ShaderUniform level = UniformOf("level", 1, false, 0), mode = UniformOf("mode", 1, true, 2), tint = UniformOf("tint", 3, false, 1);
	level.perActor = mode.perActor = tint.perActor = true;
	EXPECT_EQ("#define level (fuaActorUniform(0).x)\n#define mode int(fuaActorUniform(2).x)\n#define tint (fuaActorUniform(1).xyz)\n", ComputeUniformLines({ level, mode, tint }));
	UserShaderKey a = KeyOf("a.fp"), b = a;
	b.actorMaterial = 0;
	EXPECT_FALSE(ComputeSameUserShader(a, b));
	EXPECT_NE(nullptr, ComputeGLRefusal(b, kEntryTexel));
	a.uniforms = { UniformOf("level", 1, false, 0) };
	b = a;
	b.uniforms[0].perActor = true;
	EXPECT_FALSE(ComputeSameUserShader(a, b));
}

// [Doruk-Aksoy] A shader's own declaration of a uniform the program gives goes, in any spacing, and every other line stays.
TEST(HwShader, AShadersOwnUniformDeclarationGoes)
{
	const std::vector<ShaderUniform> u = { UniformOf("glow", 1, false, 0), UniformOf("mode", 1, true, 1) };
	EXPECT_EQ("a\n\nb\nuniform vec2 glow;\nuniform float other;\n",
		ComputeDropUniformDeclarations("a\n  uniform float  glow ;\n\nb\nuniform int mode;\nuniform vec2 glow;\nuniform float other;\n", u));
	EXPECT_EQ("left alone", ComputeDropUniformDeclarations("left alone", {}));
}

// [Doruk-Aksoy] Two keys with uniforms apart in number, name, components, kind or slot are two shaders.
TEST(HwShader, KeysWithOtherUniformsDiffer)
{
	UserShaderKey a = KeyOf("a.fp");
	a.uniforms = { UniformOf("glow", 1, false, 0) };
	UserShaderKey b = a;
	EXPECT_TRUE(ComputeSameUserShader(a, b));
	b.uniforms.clear();
	EXPECT_FALSE(ComputeSameUserShader(a, b));
	for (int field = 0; field < 4; field++)
	{
		b = a;
		ShaderUniform &u = b.uniforms[0];
		if (field == 0) u.name = "other";
		else if (field == 1) u.components = 2;
		else if (field == 2) u.isInt = true;
		else u.slot = 1;
		EXPECT_FALSE(ComputeSameUserShader(a, b)) << field;
	}
}

// [Doruk-Aksoy] UZDoom's types from a texture's maps: normal and specular before PBR's four, anything less the default.
TEST(HwShader, AMaterialsMapsGiveItsType)
{
	EXPECT_EQ(kMaterialSpecular, ComputeMaterialTypeFromLayers(true, true, false, false, false));
	EXPECT_EQ(kMaterialSpecular, ComputeMaterialTypeFromLayers(true, true, true, true, true));
	EXPECT_EQ(kMaterialPBR, ComputeMaterialTypeFromLayers(true, false, true, true, true));
	EXPECT_EQ(kMaterialDefault, ComputeMaterialTypeFromLayers(false, true, true, true, true));
	EXPECT_EQ(kMaterialDefault, ComputeMaterialTypeFromLayers(true, false, true, true, false));
	EXPECT_EQ(kMaterialDefault, ComputeMaterialTypeFromLayers(true, false, false, false, false));
	EXPECT_EQ(kMaterialDefault, ComputeMaterialTypeFromLayers(false, false, false, false, false));
}

// [Doruk-Aksoy] A texture's own shader runs when its type is its maps', else the built-in one for its maps' type, and nothing for the default.
TEST(HwShader, ATexturesShaderMustFitItsMaps)
{
	EXPECT_EQ(3, ComputeMaterialShader(3, kMaterialDefault, kMaterialDefault, 7));
	EXPECT_EQ(3, ComputeMaterialShader(3, kMaterialPBR, kMaterialPBR, 7));
	EXPECT_EQ(7, ComputeMaterialShader(3, kMaterialDefault, kMaterialSpecular, 7));
	EXPECT_EQ(-1, ComputeMaterialShader(3, kMaterialPBR, kMaterialDefault, 7));
	EXPECT_EQ(7, ComputeMaterialShader(-1, kMaterialDefault, kMaterialPBR, 7));
	EXPECT_EQ(-1, ComputeMaterialShader(-1, kMaterialDefault, kMaterialDefault, 7));
	EXPECT_EQ(-1, ComputeMaterialShader(-1, kMaterialDefault, kMaterialSpecular, -1));
}

// [Doruk-Aksoy] A built-in shader's key has no lump and its type, one per type, and its source fills the material at the texture's coordinate.
TEST(HwShader, ABuiltInShaderIsOnePerType)
{
	std::vector<UserShaderKey> shaders = { KeyOf("a.fp") };
	EXPECT_EQ(1, ComputeUserShaderSlot(shaders, ComputeBuiltinMaterialKey(kMaterialSpecular)));
	EXPECT_EQ(2, ComputeUserShaderSlot(shaders, ComputeBuiltinMaterialKey(kMaterialPBR)));
	EXPECT_EQ(1, ComputeUserShaderSlot(shaders, ComputeBuiltinMaterialKey(kMaterialSpecular)));
	EXPECT_TRUE(shaders[1].lump.empty());
	std::string adapted;
	const ShaderEntries e = ComputeAdaptUserShader(ComputeBuiltinMaterialSource(kMaterialSpecular), 1, adapted);
	EXPECT_EQ(kEntrySetupMaterial, ComputeProgramEntry(e, kLightingZandronum));
	EXPECT_TRUE(Has(adapted, "void fuaSetupMaterial_1(inout Material material)"));
	EXPECT_EQ("the built-in specular material", ComputeUserShaderName(shaders[1]));
	EXPECT_EQ("the built-in PBR material", ComputeUserShaderName(shaders[2]));
	EXPECT_EQ("the built-in material", ComputeUserShaderName(ComputeBuiltinMaterialKey(kMaterialDefault)));
	EXPECT_EQ("a.fp", ComputeUserShaderName(shaders[0]));
}

// [Doruk-Aksoy] A key's uniforms reach its program between its defines, its own declaration of one dropped.
TEST(HwShader, AKeysUniformsReachItsProgram)
{
	UserShaderKey k = KeyOf("a.fp");
	k.defines = { { "A", "1" } };
	k.uniforms = { UniformOf("glow", 1, false, 5) };
	const std::string b = ComputeUserProgramBlock(kSceneProgram, "uniform float glow;\nX\n", SpecOf(kEntryTexel, kLightNone, &k), 0);
	EXPECT_TRUE(Has(b, "#define A 1\n#define glow (uFuaTexUniforms[5].x)\nX\n#undef glow\n#undef A\n"));
	EXPECT_FALSE(Has(b, "uniform float glow;"));
}

// [Doruk-Aksoy] One lump named twice in any case is one shader, but lit, typed, textured or defined apart it is another, each added once at the end.
TEST(HwShader, AShaderIsOneSlotPerKey)
{
	std::vector<UserShaderKey> shaders;
	EXPECT_EQ(0, ComputeUserShaderSlot(shaders, KeyOf("shaders/glow.fp")));
	EXPECT_EQ(0, ComputeUserShaderSlot(shaders, KeyOf("SHADERS/Glow.fp")));
	EXPECT_EQ(1, ComputeUserShaderSlot(shaders, KeyOf("shaders/glow.fp", kLightingUZDoom)));
	EXPECT_EQ(2, ComputeUserShaderSlot(shaders, KeyOf("shaders/glow2.fp")));
	EXPECT_EQ(3, ComputeUserShaderSlot(shaders, KeyOf("shaders/glo.fp")));
	EXPECT_EQ(1, ComputeUserShaderSlot(shaders, KeyOf("Shaders/GLOW.FP", kLightingUZDoom)));
	UserShaderKey typed = KeyOf("shaders/glow.fp");
	typed.type = kMaterialSpecular;
	EXPECT_EQ(4, ComputeUserShaderSlot(shaders, typed));
	UserShaderKey textured = KeyOf("shaders/glow.fp");
	textured.textures = { "noise" };
	EXPECT_EQ(5, ComputeUserShaderSlot(shaders, textured));
	UserShaderKey defined = KeyOf("shaders/glow.fp");
	defined.defines = { { "A", "1" } };
	EXPECT_EQ(6, ComputeUserShaderSlot(shaders, defined));
	defined.defines[0].value = "2";
	EXPECT_EQ(7, ComputeUserShaderSlot(shaders, defined));
	defined.defines[0].name = "B";
	EXPECT_EQ(8, ComputeUserShaderSlot(shaders, defined));
	defined.defines.push_back({ "C", "" });
	EXPECT_EQ(9, ComputeUserShaderSlot(shaders, defined));
	EXPECT_EQ(9, ComputeUserShaderSlot(shaders, defined));
	UserShaderKey named = KeyOf("shaders/glow.fp");
	named.uniformSet = "lava";
	EXPECT_EQ(10, ComputeUserShaderSlot(shaders, named));
	named.uniformSet = "LAVA";
	EXPECT_EQ(10, ComputeUserShaderSlot(shaders, named));
	ASSERT_EQ(11u, shaders.size());
	EXPECT_EQ("shaders/glow.fp", shaders[1].lump);
	EXPECT_EQ(kLightingUZDoom, shaders[1].lighting);
	EXPECT_FALSE(ComputeSameUserShader(KeyOf("a.fp"), KeyOf("ab.fp")));
}

// [Doruk-Aksoy] UZDoom's material types by every name, in any case, and an unknown one leaves the type alone.
TEST(HwShader, AMaterialTypeIsReadInAnyCase)
{
	MaterialType t = kMaterialPBR;
	EXPECT_TRUE(ComputeMaterialTypeOf("Normal", t));
	EXPECT_EQ(kMaterialDefault, t);
	EXPECT_TRUE(ComputeMaterialTypeOf("brightmap", t));
	EXPECT_EQ(kMaterialDefault, t);
	EXPECT_TRUE(ComputeMaterialTypeOf("SPECULAR", t));
	EXPECT_EQ(kMaterialSpecular, t);
	EXPECT_TRUE(ComputeMaterialTypeOf("specularbrightmap", t));
	EXPECT_EQ(kMaterialSpecular, t);
	EXPECT_TRUE(ComputeMaterialTypeOf("pbr", t));
	EXPECT_EQ(kMaterialPBR, t);
	EXPECT_TRUE(ComputeMaterialTypeOf("PBRBrightmap", t));
	EXPECT_FALSE(ComputeMaterialTypeOf("metal", t));
	EXPECT_EQ(kMaterialPBR, t);
}

// [Doruk-Aksoy] UZDoom's textureN per type, texture2 first: its maps, then the custom textures up to texture12.
TEST(HwShader, NumberedSamplersFollowEachTypesLayers)
{
	EXPECT_EQ(8, ComputeCustomTextureLimit(kMaterialDefault));
	EXPECT_EQ(6, ComputeCustomTextureLimit(kMaterialSpecular));
	EXPECT_EQ(4, ComputeCustomTextureLimit(kMaterialPBR));
	EXPECT_EQ(kSamplerNone, ComputeNumberedSampler(kMaterialDefault, 1));
	EXPECT_EQ(kSamplerNone, ComputeNumberedSampler(kMaterialDefault, 13));
	EXPECT_EQ(kSamplerBrightmap, ComputeNumberedSampler(kMaterialDefault, 2));
	EXPECT_EQ(kLayerDetail, ComputeNumberedSampler(kMaterialDefault, 3));
	EXPECT_EQ(kLayerGlow, ComputeNumberedSampler(kMaterialDefault, 4));
	EXPECT_EQ(kLayerCustom, ComputeNumberedSampler(kMaterialDefault, 5));
	EXPECT_EQ(kLayerCustom + 7, ComputeNumberedSampler(kMaterialDefault, 12));
	EXPECT_EQ(kLayerNormal, ComputeNumberedSampler(kMaterialSpecular, 2));
	EXPECT_EQ(kLayerSpecular, ComputeNumberedSampler(kMaterialSpecular, 3));
	EXPECT_EQ(kSamplerBrightmap, ComputeNumberedSampler(kMaterialSpecular, 4));
	EXPECT_EQ(kLayerCustom, ComputeNumberedSampler(kMaterialSpecular, 7));
	EXPECT_EQ(kLayerNormal, ComputeNumberedSampler(kMaterialPBR, 2));
	EXPECT_EQ(kLayerMetallic, ComputeNumberedSampler(kMaterialPBR, 3));
	EXPECT_EQ(kLayerRoughness, ComputeNumberedSampler(kMaterialPBR, 4));
	EXPECT_EQ(kLayerAO, ComputeNumberedSampler(kMaterialPBR, 5));
	EXPECT_EQ(kSamplerBrightmap, ComputeNumberedSampler(kMaterialPBR, 6));
	EXPECT_EQ(kLayerCustom, ComputeNumberedSampler(kMaterialPBR, 9));
	EXPECT_EQ(kLayerCustom + 3, ComputeNumberedSampler(kMaterialPBR, 12));
}

// [Doruk-Aksoy] A Texture line needs a name GLSL can take, given once, within the type's texture units.
TEST(HwShader, ATextureLineIsRefusedForABadNameARepeatOrNoRoom)
{
	const std::vector<std::string> names = { "noise", "mask" };
	EXPECT_EQ(nullptr, ComputeCustomTextureRefusal(names, "_glow2", 8));
	EXPECT_NE(nullptr, ComputeCustomTextureRefusal(names, "", 8));
	EXPECT_NE(nullptr, ComputeCustomTextureRefusal(names, "2tex", 8));
	EXPECT_NE(nullptr, ComputeCustomTextureRefusal(names, "my-tex", 8));
	EXPECT_STREQ("is defined twice", ComputeCustomTextureRefusal(names, "mask", 8));
	EXPECT_STREQ("is past the last texture unit this shader type has", ComputeCustomTextureRefusal(names, "more", 2));
}

// [Doruk-Aksoy] Defines become #define lines in order, a bare one without a value, and #undef lines take them back.
TEST(HwShader, DefinesAreWrittenAndTakenBack)
{
	const std::vector<ShaderDefine> d = { { "A", "1" }, { "B", "" }, { "C", "vec3(1.0)" } };
	EXPECT_EQ("#define A 1\n#define B\n#define C vec3(1.0)\n", ComputeDefineLines(d));
	EXPECT_EQ("#undef A\n#undef B\n#undef C\n", ComputeUndefineLines(d));
	EXPECT_EQ("", ComputeDefineLines({}));
}

// [Doruk-Aksoy] Glossiness and level a byte each plus one, rounded and clamped, zero for one left at UZDoom's default.
TEST(HwShader, ASpecularWordPacksGlossinessAndLevel)
{
	EXPECT_EQ(0u, ComputeSpecularWord(false, 10.f, false, 0.1f));
	EXPECT_EQ(11u, ComputeSpecularWord(true, 10.f, false, 0.f));
	EXPECT_EQ(11u << 8, ComputeSpecularWord(false, 0.f, true, 0.1f));
	EXPECT_EQ(1u | (1u << 8), ComputeSpecularWord(true, -3.f, true, -1.f));
	EXPECT_EQ(255u | (255u << 8), ComputeSpecularWord(true, 999.f, true, 9.f));
	EXPECT_EQ(33u | (51u << 8), ComputeSpecularWord(true, 31.6f, true, 0.496f));
}
