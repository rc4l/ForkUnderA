// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwshaders/computation/hwshader_compute.h"

using namespace zx::hwshaders;

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

// [rc4l] func_warp1.fp's shape: the version and the loose timer go, ProcessTexel is renamed, and the rest is left alone.
TEST(HwShader, ANewShaderLosesItsVersionAndTimerAndIsRenamed)
{
	const std::string src = "#version 120\nuniform float timer;\n  uniform   float timer ;\nvec4 ProcessTexel()\n{\n\treturn getTexel(gl_TexCoord[0].st + timer);\n}";
	std::string out;
	ASSERT_TRUE(ComputeAdaptUserShader(src, 3, out));
	EXPECT_EQ(std::string::npos, out.find("#version"));
	EXPECT_EQ(std::string::npos, out.find("uniform"));
	EXPECT_NE(std::string::npos, out.find("vec4 fuaProcessTexel_3()"));
	EXPECT_NE(std::string::npos, out.find("getTexel(vTexCoord.st + timer)"));
	EXPECT_EQ('\n', out.back());
}

// [rc4l] An old shader defines Process(color) and may define ProcessLight; each is renamed apart, and only whole words are touched.
TEST(HwShader, AnOldShaderKeepsProcessAndWholeWordsOnly)
{
	const std::string src = "vec4 Process(vec4 color) { return getTexel(vTexCoord.st) * color; }\nvec4 ProcessLight(vec4 c) { return c; }\nfloat MyProcess = 1.0; float Processes;\n";
	std::string out;
	ASSERT_FALSE(ComputeAdaptUserShader(src, 0, out));
	EXPECT_NE(std::string::npos, out.find("vec4 fuaProcess_0(vec4 color)"));
	EXPECT_NE(std::string::npos, out.find("vec4 fuaProcessLight_0(vec4 c)"));
	EXPECT_NE(std::string::npos, out.find("MyProcess"));
	EXPECT_NE(std::string::npos, out.find("Processes"));
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
	EXPECT_EQ(std::string::npos, ComputeLegacyShaderNames("#define camerapos uCameraPos.xyz\nvec3 a = camerapos;\n").find("(uCameraPos.xyz)"));
	EXPECT_EQ(std::string::npos, ComputeLegacyShaderNames("  #  define camerapos uCameraPos.xyz\n").find("(uCameraPos.xyz)"));
	EXPECT_EQ(std::string::npos, ComputeLegacyShaderNames("vec3 camerapos = uCameraPos.xyz;\n").find("(uCameraPos.xyz)"));
	EXPECT_EQ(std::string::npos, ComputeLegacyShaderNames("float\tcamerapos;\n").find("(uCameraPos.xyz)"));
	// [Doruk-Aksoy] Only a whole word counts, as a use or as a definition.
	EXPECT_EQ(std::string::npos, ComputeLegacyShaderNames("float mycamerapos;\n").find("#define camerapos"));
	EXPECT_NE(std::string::npos, ComputeLegacyShaderNames("undefine camerapos;\nx = camerapos;\n").find("#define camerapos"));
	EXPECT_NE(std::string::npos, ComputeLegacyShaderNames("define camerapos;\n").find("#define camerapos"));
	EXPECT_NE(std::string::npos, ComputeLegacyShaderNames("vec3 mycamerapos = camerapos;\n").find("#define camerapos"));
}

// [Doruk-Aksoy] The backend's adaptation takes the legacy names too, ahead of its own renames.
TEST(HwShader, TheBackendsAdaptationTakesLegacyNames)
{
	std::string out;
	ASSERT_FALSE(ComputeAdaptUserShader("uniform vec3 camerapos;\nvec4 Process(vec4 c) { return vec4(gl_TextureMatrix[7][3].xyz - camerapos, 1.0); }\n", 2, out));
	EXPECT_EQ(0u, out.find("#define camerapos (uCameraPos.xyz)\nmat4 fuaTextureMatrix(int i)"));
	EXPECT_NE(std::string::npos, out.find("vec4 fuaProcess_2(vec4 c) { return vec4(fuaTextureMatrix(7)[3].xyz - camerapos, 1.0); }"));
}

// [Doruk-Aksoy] A shader's own program: GL's names over the scene shader's, the shader, and fuaSurfaceTexel calling its entry by style.
TEST(HwShader, AProgramBlockGivesGLsNamesAndCallsItsEntry)
{
	const std::string b = ComputeUserProgramBlock(kSceneProgram, "A\n", true, 4);
	EXPECT_NE(std::string::npos, b.find("vec4 getTexel(vec2 st)"));
	EXPECT_NE(std::string::npos, b.find("#define texture2D(s, uv) fuaTexel(uv)\n"));
	EXPECT_NE(std::string::npos, b.find("#define texture(s, uv) fuaTexel(uv)\n"));
	EXPECT_NE(std::string::npos, b.find("#define pixelpos vPixelPos\n"));
	EXPECT_NE(std::string::npos, b.find("#define glowdist vGlowDist\n"));
	EXPECT_NE(std::string::npos, b.find("#define uCameraPos vec4(uCameraPos.xyz, 0.0)\n"));
	EXPECT_NE(std::string::npos, b.find("#define vColor vec4(vColor, vLightParm.z)\n"));
	EXPECT_NE(std::string::npos, b.find("#define uDesaturationFactor vDesaturation\n"));
	EXPECT_NE(std::string::npos, b.find("#define ModelMatrix mat4(vModel0, vModel1, vModel2, vModel3)\n"));
	EXPECT_NE(std::string::npos, b.find("layout(location = 23) flat in vec4 vModel0;\n"));
	EXPECT_NE(std::string::npos, b.find("layout(location = 26) flat in vec4 vModel3;\n"));
	EXPECT_NE(std::string::npos, b.find("#define TextureMatrix mat4(1.0)\nA\n#undef texture2D\n"));
	EXPECT_NE(std::string::npos, b.find("#undef TextureMatrix\nvec4 fuaSurfaceTexel(vec2 fuaUV) {\n"));
	EXPECT_NE(std::string::npos, b.find("    if (vWarp.x < 2.5) return fuaTexMode(fuaTexel(fuaUV));\n"));
	EXPECT_NE(std::string::npos, b.find("    timer = uFrame.x * vWarp.y;\n"));
	EXPECT_NE(std::string::npos, b.find("    return fuaProcessTexel_4();\n"));
	EXPECT_NE(std::string::npos, b.find("float fuaSurfaceAlpha() { return vLightParm.z; }\n"));
	// [Doruk-Aksoy] Every name the block defines is undefined again before its own code.
	for (const char *name : { "texture2D", "texture", "pixelpos", "glowdist", "uCameraPos", "vColor", "uDesaturationFactor", "ModelMatrix", "TextureMatrix" })
		EXPECT_NE(std::string::npos, b.find(std::string("#undef ") + name + "\n")) << name;
	// [Doruk-Aksoy] An old shader's entry is Process, handed white and the piece's alpha, as 3.3 handed it, and the alpha it returns is the one drawn.
	const std::string old = ComputeUserProgramBlock(kSceneProgram, "B\n", false, 0);
	EXPECT_NE(std::string::npos, old.find("    return fuaProcess_0(vec4(1.0, 1.0, 1.0, vLightParm.z));\n"));
	EXPECT_NE(std::string::npos, old.find("float fuaSurfaceAlpha() { return vWarp.x < 2.5 ? vLightParm.z : 1.0; }\n"));
}

// [Doruk-Aksoy] A 2D program reads the 2D layer's texel and colour, has no world to place, and its old entry is handed the draw's colour whole.
TEST(HwShader, A2DProgramBlockTakesThe2DLayersNames)
{
	const std::string b = ComputeUserProgramBlock(k2DProgram, "C\n", true, 2);
	EXPECT_EQ(std::string::npos, b.find("vModel0"));
	EXPECT_NE(std::string::npos, b.find("vec4 getTexel(vec2 st) { return fua2DTexel(st); }\n"));
	EXPECT_NE(std::string::npos, b.find("#define texture2D(s, uv) fua2DRaw(uv)\n"));
	EXPECT_NE(std::string::npos, b.find("#define pixelpos vec4(gl_FragCoord.xy, 0.0, 0.0)\n"));
	EXPECT_NE(std::string::npos, b.find("#define ModelMatrix mat4(1.0)\n"));
	EXPECT_EQ(std::string::npos, b.find("#define vColor"));
	EXPECT_NE(std::string::npos, b.find("vec4 fua2DColor(vec2 fuaUV) {\n    vTexCoord = vec4(fuaUV, 0.0, 1.0);\n    timer = uInvScreen.z * vSpeed;\n"));
	EXPECT_NE(std::string::npos, b.find("    return fuaProcessTexel_2() * vColor;\n"));
	EXPECT_EQ(std::string::npos, b.find("fuaSurfaceAlpha"));
	EXPECT_NE(std::string::npos, ComputeUserProgramBlock(k2DProgram, "D\n", false, 1).find("    return fuaProcess_1(vColor);\n"));
}

TEST(HwShader, APlainBlockSamplesTheTexture)
{
	EXPECT_EQ("vec4 fuaSurfaceTexel(vec2 fuaUV) { return fuaTexMode(fuaTexel(fuaUV)); }\nfloat fuaSurfaceAlpha() { return vLightParm.z; }\n",
		ComputePlainProgramBlock(kSceneProgram));
	EXPECT_EQ("vec4 fua2DColor(vec2 fuaUV) { return fua2DTexel(fuaUV) * vColor; }\n", ComputePlainProgramBlock(k2DProgram));
}

// [Doruk-Aksoy] Each kind marks its block's place apart, and neither marker is found inside the other.
TEST(HwShader, EachKindHasItsOwnMarker)
{
	const std::string scene = ComputeProgramMarker(kSceneProgram), twoD = ComputeProgramMarker(k2DProgram);
	EXPECT_EQ("//FUA_USER_SHADERS", scene);
	EXPECT_EQ("//FUA_2D_SHADERS", twoD);
	EXPECT_EQ(std::string::npos, scene.find(twoD));
	EXPECT_EQ(std::string::npos, twoD.find(scene));
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
