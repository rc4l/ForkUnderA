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

// [rc4l] The block gives the shims, each shader's entry by its style, and the piece's texel through fuaSurfaceTexel.
TEST(HwShader, TheBlockCallsEachShaderByItsEntry)
{
	const std::string b = ComputeUserShaderBlock({ "A\n", "B\n" }, { true, false });
	EXPECT_NE(std::string::npos, b.find("vec4 getTexel(vec2 st)"));
	EXPECT_NE(std::string::npos, b.find("#define texture2D(s, uv) fuaTexel(uv)"));
	EXPECT_NE(std::string::npos, b.find("A\nB\n#undef texture2D"));
	EXPECT_NE(std::string::npos, b.find("if (which == 0) return fuaProcessTexel_0();"));
	EXPECT_NE(std::string::npos, b.find("if (which == 1) return fuaProcess_1(vec4(1.0));"));
	EXPECT_NE(std::string::npos, b.find("vec4 fuaSurfaceTexel(vec2 uv)"));
	// [rc4l] With none it still gives fuaSurfaceTexel, and a style missing for a shader reads as old.
	const std::string none = ComputeUserShaderBlock({}, {});
	EXPECT_NE(std::string::npos, none.find("vec4 fuaSurfaceTexel(vec2 uv)"));
	EXPECT_EQ(std::string::npos, none.find("which =="));
	EXPECT_NE(std::string::npos, ComputeUserShaderBlock({ "C\n" }, {}).find("fuaProcess_0(vec4(1.0))"));
	// [rc4l] A shader that did not compile is left empty, and its number gets no entry.
	const std::string gap = ComputeUserShaderBlock({ "", "D\n" }, { true, true });
	EXPECT_EQ(std::string::npos, gap.find("which == 0"));
	EXPECT_NE(std::string::npos, gap.find("if (which == 1) return fuaProcessTexel_1();"));
}

TEST(HwShader, TheBlockReplacesTheMarkersLine)
{
	EXPECT_EQ("a\nBLOCK\nc\n", ComputeSpliceShaderBlock("a\n  //MARK x\nc\n", "//MARK", "BLOCK\n"));
	EXPECT_EQ("BLOCK\nc", ComputeSpliceShaderBlock("//MARK\nc", "//MARK", "BLOCK\n"));
	EXPECT_EQ("a\nBLOCK\n", ComputeSpliceShaderBlock("a\n//MARK", "//MARK", "BLOCK\n"));
	EXPECT_EQ("plain", ComputeSpliceShaderBlock("plain", "//MARK", "BLOCK\n"));
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
