// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/postprocess/computation/postprocess_compute.h"

#include <cstring>

using namespace zx::postprocess;

// [Doruk-Aksoy] The three targets UZDoom knows, in any case, and nothing else.
TEST(PostProcess, TargetsNameTheirStage)
{
	EXPECT_EQ(kStageBeforeBloom, ComputeStage("BeforeBloom"));
	EXPECT_EQ(kStageScene, ComputeStage("scene"));
	EXPECT_EQ(kStageScreen, ComputeStage("SCREEN"));
	EXPECT_EQ(-1, ComputeStage("hud"));
}

// [Doruk-Aksoy] Each uniform type word and its size, an unknown word holding nothing.
TEST(PostProcess, UniformTypesAndTheirSizes)
{
	EXPECT_EQ(kUniformInt, ComputeUniformType("INT"));
	EXPECT_EQ(kUniformFloat, ComputeUniformType("float"));
	EXPECT_EQ(kUniformVec2, ComputeUniformType("vec2"));
	EXPECT_EQ(kUniformVec3, ComputeUniformType("Vec3"));
	EXPECT_EQ(kUniformVec4, ComputeUniformType("vec4"));
	EXPECT_EQ(kUniformUndefined, ComputeUniformType("mat4"));
	EXPECT_EQ(1, ComputeUniformComponents(kUniformInt));
	EXPECT_EQ(1, ComputeUniformComponents(kUniformFloat));
	EXPECT_EQ(2, ComputeUniformComponents(kUniformVec2));
	EXPECT_EQ(3, ComputeUniformComponents(kUniformVec3));
	EXPECT_EQ(4, ComputeUniformComponents(kUniformVec4));
	EXPECT_EQ(0, ComputeUniformComponents(kUniformUndefined));
}

// [Doruk-Aksoy] A number cvar feeds a number uniform and a colour one a vector, as UZDoom checks; nothing else fits.
TEST(PostProcess, CvarsFitTheUniformsUZDoomAllows)
{
	for (const CvarKind k : { kCvarInt, kCvarFloat })
	{
		EXPECT_TRUE(ComputeCvarUniformFits(k, kUniformInt));
		EXPECT_TRUE(ComputeCvarUniformFits(k, kUniformFloat));
		EXPECT_FALSE(ComputeCvarUniformFits(k, kUniformVec3));
	}
	EXPECT_TRUE(ComputeCvarUniformFits(kCvarColor, kUniformVec3));
	EXPECT_TRUE(ComputeCvarUniformFits(kCvarColor, kUniformVec4));
	EXPECT_FALSE(ComputeCvarUniformFits(kCvarColor, kUniformFloat));
	EXPECT_FALSE(ComputeCvarUniformFits(kCvarOther, kUniformFloat));
}

// [Doruk-Aksoy] A colour cvar's channels in red, green, blue, alpha order, each out of 255.
TEST(PostProcess, AColourCvarGivesItsChannels)
{
	double v[4];
	ComputeColorUniform(0x80FF4000u, v);
	EXPECT_DOUBLE_EQ(1.0, v[0]);
	EXPECT_DOUBLE_EQ(64 / 255.0, v[1]);
	EXPECT_DOUBLE_EQ(0.0, v[2]);
	EXPECT_DOUBLE_EQ(128 / 255.0, v[3]);
}

// [Doruk-Aksoy] UZDoom's version range, both ends included.
TEST(PostProcess, ShaderVersionsFrom330To450)
{
	EXPECT_FALSE(ComputeShaderVersionValid(329));
	EXPECT_TRUE(ComputeShaderVersionValid(330));
	EXPECT_TRUE(ComputeShaderVersionValid(450));
	EXPECT_FALSE(ComputeShaderVersionValid(451));
}

// [Doruk-Aksoy] std140 offsets in declaration order, a vec3 on 16 bytes taking 12, and the block padded to 16.
TEST(PostProcess, UniformsAreLaidOutAsStd140)
{
	std::vector<UniformField> f = { { "a", kUniformFloat, 0 }, { "b", kUniformVec3, 0 }, { "c", kUniformFloat, 0 },
		{ "d", kUniformVec2, 0 }, { "e", kUniformInt, 0 }, { "f", kUniformVec4, 0 } };
	EXPECT_EQ(64u, ComputeUniformLayout(f));
	EXPECT_EQ(0u, f[0].offset);
	EXPECT_EQ(16u, f[1].offset);
	EXPECT_EQ(28u, f[2].offset);
	EXPECT_EQ(32u, f[3].offset);
	EXPECT_EQ(40u, f[4].offset);
	EXPECT_EQ(48u, f[5].offset);
	std::vector<UniformField> none;
	EXPECT_EQ(16u, ComputeUniformLayout(none));
	std::vector<UniformField> odd = { { "x", kUniformUndefined, 0 }, { "y", kUniformFloat, 0 } };
	EXPECT_EQ(16u, ComputeUniformLayout(odd));
	EXPECT_EQ(4u, odd[1].offset);
}

// [Doruk-Aksoy] An int uniform's first value as an int, a vector's components as floats, each at its offset.
TEST(PostProcess, UniformValuesAreWrittenByType)
{
	unsigned char block[32] = {};
	const double v[4] = { 3.75, -2.0, 0.5, 1.0 };
	ComputeWriteUniform(block, { "i", kUniformInt, 0 }, v);
	ComputeWriteUniform(block, { "v", kUniformVec3, 16 }, v);
	int i = 0;
	memcpy(&i, block, sizeof(i));
	EXPECT_EQ(3, i);
	float f[4];
	memcpy(f, block + 16, sizeof(f));
	EXPECT_FLOAT_EQ(3.75f, f[0]);
	EXPECT_FLOAT_EQ(-2.0f, f[1]);
	EXPECT_FLOAT_EQ(0.5f, f[2]);
	EXPECT_FLOAT_EQ(0.0f, f[3]);   // [Doruk-Aksoy] a vec3 leaves the fourth float alone
}

// [Doruk-Aksoy] The prolog declares GL's names over the backend's, the block with each offset, and each extra texture under its own name.
TEST(PostProcess, ThePrologDeclaresWhatTheShaderReads)
{
	ProgramDesc d;
	d.uniforms = { { "intensity", kUniformFloat, 0 }, { "tint", kUniformVec3, 16 } };
	d.textureNames = { "noise" };
	const std::string p = ComputePostProcessProlog(d);
	EXPECT_EQ(0u, p.find("#version 450\n"));
	EXPECT_NE(std::string::npos, p.find("uniform Uniforms"));
	EXPECT_NE(std::string::npos, p.find("layout(offset = 0) float intensity;"));
	EXPECT_NE(std::string::npos, p.find("layout(offset = 16) vec3 tint;"));
	EXPECT_NE(std::string::npos, p.find("float InputTimeGame;"));
	EXPECT_NE(std::string::npos, p.find("float fua_EnabledTime;"));
	EXPECT_NE(std::string::npos, p.find("uniform texture2D fuaTex_noise;"));
	EXPECT_NE(std::string::npos, p.find("#define noise sampler2D(fuaTex_noise, fuaTex_noise_sampler)"));
	EXPECT_NE(std::string::npos, p.find("#define TexCoord fuaTexCoord()"));
	EXPECT_NE(std::string::npos, p.find("#define InputTexture FuaPostPicture(0)"));
	EXPECT_NE(std::string::npos, p.find("out vec4 FragColor;"));
	// [Doruk-Aksoy] Without a previous frame, both pictures read the input.
	EXPECT_EQ(std::string::npos, p.find("uniform texture2D fuaLastInput"));
	EXPECT_EQ(std::string::npos, p.find("sampler2D(fuaLastInput"));
}

// [Doruk-Aksoy] Each uniform type is declared as its GLSL type.
TEST(PostProcess, EachUniformTypeIsDeclaredAsItself)
{
	ProgramDesc d;
	d.uniforms = { { "i", kUniformInt, 0 }, { "v2", kUniformVec2, 8 }, { "v4", kUniformVec4, 16 } };
	const std::string p = ComputePostProcessProlog(d);
	EXPECT_NE(std::string::npos, p.find("layout(offset = 0) int i;"));
	EXPECT_NE(std::string::npos, p.find("layout(offset = 8) vec2 v2;"));
	EXPECT_NE(std::string::npos, p.find("layout(offset = 16) vec4 v4;"));
}

// [Doruk-Aksoy] A shader with no uniforms has no block of them, and one reading the previous frame gets its texture.
TEST(PostProcess, ThePrologLeavesOutWhatIsNotThere)
{
	ProgramDesc d;
	d.lastInput = true;
	const std::string p = ComputePostProcessProlog(d);
	EXPECT_EQ(std::string::npos, p.find("uniform Uniforms"));
	EXPECT_NE(std::string::npos, p.find("uniform texture2D fuaLastInput;"));
	EXPECT_NE(std::string::npos, p.find("sampler2D(fuaLastInput, fuaLastInput_sampler)"));
}

// [Doruk-Aksoy] Only #version lines go, indented ones too.
TEST(PostProcess, AModsCodeLosesItsVersion)
{
	EXPECT_EQ("void main()\n{\n}\n", ComputeAdaptPostProcessShader("  #version 330\nvoid main()\n{\n}"));
	EXPECT_EQ("\n#define X 1\n", ComputeAdaptPostProcessShader("\n#define X 1\n#version 450\n"));
}

// [Doruk-Aksoy] Only what the compiled shader still has is laid out, pictures clamping and Texture lines repeating; a shader reading nothing lays out nothing.
TEST(PostProcess, TheLayoutIsWhatTheShaderKept)
{
	const std::vector<LayoutTexture> all = ComputeLayoutTextures(true, { "noise", "unused" }, { "fuaInput", "fuaInput_sampler", "fuaLastInput", "fuaTex_noise", "fuaPostAuto" });
	ASSERT_EQ(3u, all.size());
	EXPECT_EQ("fuaInput", all[0].name);
	EXPECT_TRUE(all[0].clamp);
	EXPECT_EQ("fuaLastInput", all[1].name);
	EXPECT_EQ("fuaTex_noise", all[2].name);
	EXPECT_FALSE(all[2].clamp);
	EXPECT_TRUE(ComputeLayoutTextures(false, { "noise" }, {}).empty());
	EXPECT_TRUE(ComputeLayoutTextures(false, {}, { "fuaLastInput" }).empty());
}

// [Doruk-Aksoy] A left-out shader is named with its reasons indented under it.
TEST(PostProcess, ALeftOutShaderIsNoted)
{
	EXPECT_EQ("vulkan: post-process shader vig does not compile here; it is left out\n  line 3: oops\n", ComputePostShaderSkipNote("vulkan", "vig", "line 3: oops\n\n"));
	EXPECT_EQ("vulkan: post-process shader vig does not compile here; it is left out\n", ComputePostShaderSkipNote("vulkan", "vig", ""));
}

TEST(PostProcess, ThePreviousFrameIsCopiedOnlyForAShaderThatReadsIt)
{
	EXPECT_TRUE(ComputeReadsLastInput("FragColor = texture(LastInputTexture, TexCoord);"));
	EXPECT_FALSE(ComputeReadsLastInput("FragColor = texture(InputTexture, TexCoord);"));
}

// [Doruk-Aksoy] Turning a shader on starts its clock once, a run-out one starts it again, and each change bumps the generation.
TEST(PostProcess, EnablingKeepsOrRestartsTheClock)
{
	ShaderTable t;
	ComputeSetShaderEnabled(t, "Fire", 2, true, 100, 135);
	ASSERT_EQ(1u, t.entries.size());
	EXPECT_EQ(100, t.entries[0].enabledTic);
	EXPECT_EQ(1u, t.generation);
	ComputeSetShaderEnabled(t, "fire", 2, true, 120, 170);
	EXPECT_EQ(1u, t.entries.size());
	EXPECT_EQ(100, t.entries[0].enabledTic);
	EXPECT_EQ(170, t.entries[0].expiresTic);
	ComputeSetShaderEnabled(t, "fire", 2, true, 200, 0);
	EXPECT_EQ(200, t.entries[0].enabledTic);
	ComputeSetShaderEnabled(t, "fire", 2, false, 210, 300);
	EXPECT_EQ(0, t.entries[0].enabled);
	EXPECT_EQ(0, t.entries[0].expiresTic);
	EXPECT_EQ(4u, t.generation);
}

// [Doruk-Aksoy] A uniform is added once and then overwritten, each change bumping the generation.
TEST(PostProcess, UniformsAreSetOncePerName)
{
	ShaderTable t;
	const double a[4] = { 1, 2, 3, 4 }, b[4] = { 5, 6, 7, 8 };
	ComputeSetShaderUniform(t, "vig", -1, "falloff", a);
	ComputeSetShaderUniform(t, "vig", -1, "falloff", b);
	ComputeSetShaderUniform(t, "vig", -1, "intensity", a);
	ASSERT_EQ(1u, t.entries.size());
	ASSERT_EQ(2u, t.entries[0].uniforms.size());
	EXPECT_EQ(5, t.entries[0].uniforms[0].values[0]);
	EXPECT_EQ(4, t.entries[0].uniforms[0].kind);
	ComputeSetShaderUniform(t, "vig", -1, "intensity", a, 0);
	EXPECT_EQ(0, t.entries[0].uniforms[1].kind);
	EXPECT_EQ(4u, t.generation);
}

// [Doruk-Aksoy] Asking again for what a shader already is changes nothing, while a timed call always does, as it moves the end.
TEST(PostProcess, OnlyARealChangeCounts)
{
	ShaderTable t;
	EXPECT_TRUE(ComputeSetShaderEnabled(t, "fire", -1, true, 10, 0));
	EXPECT_FALSE(ComputeSetShaderEnabled(t, "FIRE", -1, true, 20, 0));
	EXPECT_EQ(1u, t.generation);
	EXPECT_TRUE(ComputeSetShaderEnabled(t, "fire", -1, true, 30, 65));
	EXPECT_TRUE(ComputeSetShaderEnabled(t, "fire", -1, true, 40, 75));
	EXPECT_EQ(10, t.entries[0].enabledTic);
	EXPECT_EQ(75, t.entries[0].expiresTic);
	EXPECT_TRUE(ComputeSetShaderEnabled(t, "fire", -1, false, 50, 0));
	EXPECT_FALSE(ComputeSetShaderEnabled(t, "fire", -1, false, 60, 90));
	EXPECT_EQ(4u, t.generation);
	const double a[4] = { 1, 0, 0, 1 }, b[4] = { 2, 0, 0, 1 };
	EXPECT_TRUE(ComputeSetShaderUniform(t, "fire", -1, "x", a, 1));
	EXPECT_FALSE(ComputeSetShaderUniform(t, "fire", -1, "x", a, 1));
	EXPECT_TRUE(ComputeSetShaderUniform(t, "fire", -1, "x", a, 0));
	EXPECT_TRUE(ComputeSetShaderUniform(t, "fire", -1, "x", b, 0));
	EXPECT_EQ(7u, t.generation);
}

// [Doruk-Aksoy] A joiner told how long a shader ran starts its clock that far back, and one already running keeps its own.
TEST(PostProcess, AClockCanStartEarlier)
{
	ShaderTable t;
	ComputeSetShaderEnabled(t, "fire", -1, true, 500, 0, 430);
	EXPECT_EQ(430, ComputeShaderEntry(t, "fire", -1)->enabledTic);
	ComputeSetShaderEnabled(t, "fire", -1, true, 510, 600, 200);
	EXPECT_EQ(430, ComputeShaderEntry(t, "fire", -1)->enabledTic);
	EXPECT_EQ(nullptr, ComputeShaderEntry(t, "fire", 3));
}

// [Doruk-Aksoy] A round reset empties the level's state, bumping the generation only when there was some.
TEST(PostProcess, ARoundResetEmptiesTheTable)
{
	ShaderTable t;
	ComputeClearShaderTable(t);
	EXPECT_EQ(0u, t.generation);
	ComputeSetShaderEnabled(t, "fire", 2, true, 0, 0);
	ComputeClearShaderTable(t);
	EXPECT_TRUE(t.entries.empty());
	EXPECT_EQ(2u, t.generation);
}

// [Doruk-Aksoy] A call is checked against GLDEFS: the shader by name in any case, a uniform by its exact name on any declaration of that name.
TEST(PostProcess, OnlyDeclaredNamesAreTaken)
{
	const std::vector<DeclaredShader> d = { { "Fire", { "strength" } }, { "vig", {} }, { "fire", { "tint" } } };
	EXPECT_TRUE(ComputeShaderCallDeclared(d, "FIRE", ""));
	EXPECT_TRUE(ComputeShaderCallDeclared(d, "fire", "strength"));
	EXPECT_TRUE(ComputeShaderCallDeclared(d, "fire", "tint"));
	EXPECT_FALSE(ComputeShaderCallDeclared(d, "fire", "Strength"));
	EXPECT_FALSE(ComputeShaderCallDeclared(d, "vig", "x"));
	EXPECT_FALSE(ComputeShaderCallDeclared(d, "nosuch", ""));
	EXPECT_FALSE(ComputeShaderCallDeclared({}, "fire", ""));
}

// [Doruk-Aksoy] A server reads a lump's includes and post-process blocks in order, skipping every other block, comments and strings with braces in them.
TEST(PostProcess, AServerReadsOnlyThePostProcessBlocks)
{
	const std::string lump =
		"// a light { not a block\n"
		"pointlight L { color 1 0 0 size 64 }\n"
		"#include \"lights/more.txt\"\n"
		"HardwareShader texture STARTAN2 { Shader \"x.fp\" Speed 1 }\n"
		"/* HardwareShader PostProcess scene { Name \"hidden\" } */\n"
		"HardwareShader PostProcess scene\n{\n\tName \"fire\" // brace } in a comment\n\tShader \"s/a\\\"b{.fp\" 330\n\tUniform vec3 c\n}\n"
		"brightmap texture X { map \"y\" }\n"
		"}\n"
		"hardwareshader postprocess screen { Name \"vig\" Enabled }";
	const std::vector<DeclarationPart> parts = ComputePostProcessDeclarations(lump);
	ASSERT_EQ(3u, parts.size());
	EXPECT_TRUE(parts[0].include);
	EXPECT_EQ("lights/more.txt", parts[0].text);
	EXPECT_FALSE(parts[1].include);
	EXPECT_EQ(" scene\n{\n\tName \"fire\" // brace } in a comment\n\tShader \"s/a\\\"b{.fp\" 330\n\tUniform vec3 c\n}", parts[1].text);
	EXPECT_EQ(" screen { Name \"vig\" Enabled }", parts[2].text);
}

// [Doruk-Aksoy] What a broken lump leaves: an include with no name, a block cut short, an unclosed string or comment, and a last word.
TEST(PostProcess, ABrokenLumpEndsWhereItEnds)
{
	EXPECT_TRUE(ComputePostProcessDeclarations("#include lights").empty());
	EXPECT_TRUE(ComputePostProcessDeclarations("HardwareShader").empty());
	EXPECT_TRUE(ComputePostProcessDeclarations("HardwareShader \"PostProcess\" scene { }").empty());
	EXPECT_TRUE(ComputePostProcessDeclarations("/* never closed HardwareShader PostProcess scene { }").empty());
	std::vector<DeclarationPart> cut = ComputePostProcessDeclarations("HardwareShader PostProcess scene { Name \"x\"");
	ASSERT_EQ(1u, cut.size());
	EXPECT_EQ(" scene { Name \"x\"", cut[0].text);
	cut = ComputePostProcessDeclarations("HardwareShader PostProcess scene { Name \"never closed");
	ASSERT_EQ(1u, cut.size());
	EXPECT_EQ(" scene { Name \"never closed", cut[0].text);
	cut = ComputePostProcessDeclarations("HardwareShader PostProcess scene } { }");
	ASSERT_EQ(1u, cut.size());
	EXPECT_EQ(" scene } { }", cut[0].text);
	cut = ComputePostProcessDeclarations("HardwareShader PostProcess scene { \"x\\");
	ASSERT_EQ(1u, cut.size());
	EXPECT_EQ(" scene { \"x\\", cut[0].text);
	EXPECT_TRUE(ComputePostProcessDeclarations("HardwareShader // trailing").empty());
}

// [Doruk-Aksoy] A word ends at a brace, a quote or a comment written right against it.
TEST(PostProcess, WordsEndAtWhatFollowsThem)
{
	const std::vector<DeclarationPart> parts = ComputePostProcessDeclarations("HardwareShader PostProcess scene{Name//c\n\"a\" Shader\"b\" Enabled/*e*/ x}tail");
	ASSERT_EQ(1u, parts.size());
	EXPECT_EQ(" scene{Name//c\n\"a\" Shader\"b\" Enabled/*e*/ x}", parts[0].text);
}

// [Doruk-Aksoy] The declaration until a level says otherwise, a player's own over everyone's, and a run-out timed one as if never set.
TEST(PostProcess, TheConsolePlayersOwnStateWins)
{
	ShaderTable t;
	EXPECT_TRUE(ComputeEffectiveEnabled(t, "vig", 0, true, 10).enabled);
	EXPECT_FALSE(ComputeEffectiveEnabled(t, "vig", 0, false, 10).enabled);
	ComputeSetShaderEnabled(t, "vig", -1, true, 5, 0);
	EffectiveEnabled e = ComputeEffectiveEnabled(t, "VIG", 0, false, 10);
	EXPECT_TRUE(e.enabled);
	EXPECT_EQ(5, e.sinceTic);
	ComputeSetShaderEnabled(t, "vig", 0, false, 8, 0);
	EXPECT_FALSE(ComputeEffectiveEnabled(t, "vig", 0, false, 10).enabled);
	EXPECT_TRUE(ComputeEffectiveEnabled(t, "vig", 1, false, 10).enabled);
	EXPECT_TRUE(ComputeEffectiveEnabled(t, "vig", -1, false, 10).enabled);
	ComputeSetShaderEnabled(t, "vig", 0, true, 20, 30);
	e = ComputeEffectiveEnabled(t, "vig", 0, false, 25);
	EXPECT_TRUE(e.enabled);
	EXPECT_EQ(20, e.sinceTic);
	e = ComputeEffectiveEnabled(t, "vig", 0, false, 30);
	EXPECT_TRUE(e.enabled);
	EXPECT_EQ(5, e.sinceTic);
	// [Doruk-Aksoy] An entry that only set uniforms leaves the state alone.
	const double v[4] = { 1, 0, 0, 1 };
	ComputeSetShaderUniform(t, "other", -1, "x", v);
	EXPECT_TRUE(ComputeEffectiveEnabled(t, "other", 0, true, 10).enabled);
}

// [Doruk-Aksoy] A player's own uniform over everyone's, and none for one never set.
TEST(PostProcess, TheConsolePlayersOwnUniformWins)
{
	ShaderTable t;
	const double all[4] = { 1, 1, 1, 1 }, own[4] = { 2, 2, 2, 2 };
	EXPECT_EQ(nullptr, ComputeEffectiveUniform(t, "vig", 0, "x"));
	ComputeSetShaderUniform(t, "vig", -1, "x", all);
	EXPECT_EQ(1, ComputeEffectiveUniform(t, "vig", 0, "x")[0]);
	ComputeSetShaderUniform(t, "vig", 0, "x", own);
	EXPECT_EQ(2, ComputeEffectiveUniform(t, "vig", 0, "x")[0]);
	EXPECT_EQ(1, ComputeEffectiveUniform(t, "vig", 3, "x")[0]);
	EXPECT_EQ(1, ComputeEffectiveUniform(t, "vig", -1, "x")[0]);
	EXPECT_EQ(nullptr, ComputeEffectiveUniform(t, "vig", 0, "y"));
}

// [Doruk-Aksoy] The state line names the shader, whether it runs and since when, then each uniform.
TEST(PostProcess, AShadersStateLine)
{
	EXPECT_EQ("vig off", ComputeShaderStateLine("vig", { false, 0 }, {}));
	EXPECT_EQ("fire on since 35 alpha=0.5,0,0,1 one=1,0,0,1", ComputeShaderStateLine("fire", { true, 35 }, { { "alpha", { 0.5, 0, 0, 1 } }, { "one", { 1, 0, 0, 1 } } }));
}

TEST(PostProcess, TheConsoleTogglesOnMinusOne)
{
	EXPECT_FALSE(ComputeConsoleEnable(-1, true));
	EXPECT_TRUE(ComputeConsoleEnable(-1, false));
	EXPECT_FALSE(ComputeConsoleEnable(0, true));
	EXPECT_TRUE(ComputeConsoleEnable(1, false));
	EXPECT_TRUE(ComputeConsoleEnable(5, false));
}

// [Doruk-Aksoy] A stage runs its own shaders that are on and made, in GLDEFS order.
TEST(PostProcess, AStageRunsItsShadersThatAreOnAndMade)
{
	const std::vector<int> stages = { kStageScene, kStageScreen, kStageScene, kStageScene, kStageScene };
	const std::vector<bool> on = { true, true, false, true, true };
	const std::vector<bool> ready = { true, true, true, false };
	EXPECT_EQ(std::vector<int>({ 0 }), ComputePostProcessChain(stages, on, ready, kStageScene));
	EXPECT_EQ(std::vector<int>({ 1 }), ComputePostProcessChain(stages, on, ready, kStageScreen));
	EXPECT_TRUE(ComputePostProcessChain(stages, { true }, ready, kStageBeforeBloom).empty());
	EXPECT_TRUE(ComputePostProcessChain(stages, { false, true }, { true }, kStageScreen).empty());
}

// [Doruk-Aksoy] Passes alternate between A and B from their source, the result in the last one's.
TEST(PostProcess, PassesPingPong)
{
	EXPECT_TRUE(ComputePingPong(0, kPictureSource).empty());
	std::vector<Pass> p = ComputePingPong(3, kPictureSource);
	ASSERT_EQ(3u, p.size());
	EXPECT_EQ(kPictureSource, p[0].source); EXPECT_EQ(kPictureA, p[0].target);
	EXPECT_EQ(kPictureA, p[1].source); EXPECT_EQ(kPictureB, p[1].target);
	EXPECT_EQ(kPictureB, p[2].source); EXPECT_EQ(kPictureA, p[2].target);
	p = ComputePingPong(2, kPictureB);
	EXPECT_EQ(kPictureA, p[0].target);
	EXPECT_EQ(kPictureB, p[1].target);
	EXPECT_EQ(kPictureSource, ComputeStageResult({}, kPictureSource));
	EXPECT_EQ(kPictureB, ComputeStageResult(ComputePingPong(2, kPictureSource), kPictureSource));
}

// [Doruk-Aksoy] Real seconds and the frame's step from the frame clock, game seconds from the tic and its share, and the time since going on never below 0.
TEST(PostProcess, TimesFollowTheClocks)
{
	Times t = ComputePostProcessTimes(2500, 2484, 70, 0.5, 35, 35);
	EXPECT_FLOAT_EQ(2.5f, t.inputTime);
	EXPECT_FLOAT_EQ(0.016f, t.inputTimeDelta);
	EXPECT_FLOAT_EQ((float)(70.5 / 35), t.inputTimeGame);
	EXPECT_FLOAT_EQ((float)(35.5 / 35), t.enabledTime);
	t = ComputePostProcessTimes(100, 200, 10, 0.0, 40, 35);
	EXPECT_FLOAT_EQ(0.f, t.inputTimeDelta);
	EXPECT_FLOAT_EQ(0.f, t.enabledTime);
}

// [Doruk-Aksoy] The automatic block in the prolog's order, a pass of no size drawing at 1 x 1.
TEST(PostProcess, TheAutomaticBlockFollowsThePrologsOrder)
{
	const Times t = { 2.f, 0.5f, 3.f, 4.f };
	const float region[4] = { 0.5f, 0.5f, 640.f, 400.f }, last[4] = { 1.f, 1.f, 1280.f, 800.f };
	float out[kAutoBlockFloats];
	ComputeAutoBlock(t, region, last, 1280, 800, out);
	EXPECT_FLOAT_EQ(0.5f, out[0]);
	EXPECT_FLOAT_EQ(2.f, out[1]);
	EXPECT_FLOAT_EQ(3.f, out[2]);
	EXPECT_FLOAT_EQ(4.f, out[3]);
	EXPECT_FLOAT_EQ(640.f, out[6]);
	EXPECT_FLOAT_EQ(1280.f, out[10]);
	EXPECT_FLOAT_EQ(1280.f, out[12]);
	EXPECT_FLOAT_EQ(800.f, out[13]);
	ComputeAutoBlock(t, region, last, 0, 0, out);
	EXPECT_FLOAT_EQ(1.f, out[12]);
	EXPECT_FLOAT_EQ(1.f, out[13]);
	EXPECT_NE(std::string::npos, ComputePostProcessProlog(ProgramDesc()).find("vec4 fuaPostOutput;"));
}

// [Doruk-Aksoy] A region's share of its target and its size, a target of no size being all of it.
TEST(PostProcess, ARegionIsItsShareAndSize)
{
	float r[4];
	ComputePostRegion(640, 400, 1280, 800, r);
	EXPECT_FLOAT_EQ(0.5f, r[0]);
	EXPECT_FLOAT_EQ(0.5f, r[1]);
	EXPECT_FLOAT_EQ(640.f, r[2]);
	EXPECT_FLOAT_EQ(400.f, r[3]);
	ComputePostRegion(0, 0, 0, 0, r);
	EXPECT_FLOAT_EQ(1.f, r[0]);
	EXPECT_FLOAT_EQ(1.f, r[1]);
	EXPECT_FLOAT_EQ(1.f, r[2]);
	EXPECT_FLOAT_EQ(1.f, r[3]);
}
