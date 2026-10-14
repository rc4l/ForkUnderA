// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/shaderdump_compute.h"

using namespace zx::hwrender;

TEST(ShaderDump, TheNameBecomesAFileNameWithTheStage)
{
	EXPECT_EQ("fua_scene_PS_opaque.frag", ComputeShaderDumpName("fua scene PS opaque", true));
	EXPECT_EQ("fua_sky_VS.vert", ComputeShaderDumpName("fua sky VS", false));
}

// [rc4l] Punctuation runs collapse to one underscore and none is left at either end.
TEST(ShaderDump, PunctuationCollapsesAndIsTrimmed)
{
	EXPECT_EQ("fua_scene_PS_masked_gbuffer.frag", ComputeShaderDumpName("  fua scene PS masked + gbuffer!", true));
}

// [rc4l] A name with nothing usable still gets a file.
TEST(ShaderDump, AnEmptyNameIsStillAFile)
{
	EXPECT_EQ("shader.vert", ComputeShaderDumpName("", false));
	EXPECT_EQ("shader.frag", ComputeShaderDumpName(nullptr, true));
	EXPECT_EQ("shader.frag", ComputeShaderDumpName("+-", true));
}
