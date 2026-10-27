// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] GLDEFS post-process shaders as the backend runs them: their uniform block, the prolog a mod's GL code is compiled under, which of them run where in a frame, and their state as a level, a player and the declaration set it.

#ifndef ZX_POSTPROCESS_COMPUTE_H
#define ZX_POSTPROCESS_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace postprocess {

// [Doruk-Aksoy] Where in the frame a shader runs: before the weapon, over the scene with it, or over the whole screen with the HUD.
enum Stage { kStageBeforeBloom, kStageScene, kStageScreen, kStageCount };

// [Doruk-Aksoy] The stage a GLDEFS target names in any case, or -1 for none of beforebloom, scene and screen.
int ComputeStage(const std::string &target);

enum UniformType { kUniformUndefined, kUniformInt, kUniformFloat, kUniformVec2, kUniformVec3, kUniformVec4 };

// [Doruk-Aksoy] The type a GLDEFS uniform's type word names in any case, kUniformUndefined for an unknown one.
UniformType ComputeUniformType(const std::string &word);

// [Doruk-Aksoy] How many numbers a uniform of `type` holds, 0 for an undefined one.
int ComputeUniformComponents(UniformType type);

// [Doruk-Aksoy] The kinds of cvar a uniform can be bound to, and the rest.
enum CvarKind { kCvarInt, kCvarFloat, kCvarColor, kCvarOther };

// [Doruk-Aksoy] Whether a cvar of `kind` can feed a uniform of `type`, as UZDoom allows: an int or float one an int or float, a colour one a vec3 or vec4.
bool ComputeCvarUniformFits(CvarKind kind, UniformType type);

// [Doruk-Aksoy] A colour cvar's packed 0xAARRGGBB value as the red, green, blue and alpha UZDoom gives its uniform, each 0..1.
void ComputeColorUniform(unsigned argb, double out[4]);

// [Doruk-Aksoy] Whether a GLDEFS shader's GLSL version is one UZDoom accepts, 330 to 450.
bool ComputeShaderVersionValid(int version);

// [Doruk-Aksoy] One uniform of a shader's block, at its byte offset.
struct UniformField
{
	std::string name;
	UniformType type;
	unsigned offset;
};

// [Doruk-Aksoy] Gives each field its std140 offset in declaration order and returns the block's size, a multiple of 16 bytes and 16 at least.
unsigned ComputeUniformLayout(std::vector<UniformField> &fields);

// [Doruk-Aksoy] Writes a field's values into its place in the block, an int one's first value as an int and a float one's as floats.
void ComputeWriteUniform(unsigned char *block, const UniformField &field, const double values[4]);

// [Doruk-Aksoy] What a shader's program declares around its own code: its uniforms, laid out, its extra textures by name, and whether it reads the previous frame.
struct ProgramDesc
{
	std::vector<UniformField> uniforms;
	std::vector<std::string> textureNames;
	bool lastInput = false;
};

// [Doruk-Aksoy] The names the backend's binding of a shader's pictures and blocks goes by.
extern const char *const kInputName;
extern const char *const kLastInputName;
extern const char *const kAutoBlockName;
extern const char *const kUniformBlockName;
// [Doruk-Aksoy] The texture variable an extra texture `name` is bound to.
std::string ComputeTextureVariable(const std::string &name);

// [Doruk-Aksoy] A texture a program's pipeline lays out, and whether it clamps (the pictures) or repeats (a Texture line's).
struct LayoutTexture
{
	std::string name;
	bool clamp;
};

// [Doruk-Aksoy] The textures to lay out: of the input, the previous frame (with `lastInput`) and the Texture lines, those the compiled shader still has (`present`), since the SPIR-V optimizer drops any it never reads.
std::vector<LayoutTexture> ComputeLayoutTextures(bool lastInput, const std::vector<std::string> &textureNames, const std::vector<std::string> &present);

// [Doruk-Aksoy] The text a mod's post-process code is compiled after, under GL's names: TexCoord and gl_FragCoord with y up, InputTexture and LastInputTexture read from the share of their target the scene covers, the time uniforms, its own uniforms and textures, and FragColor.
std::string ComputePostProcessProlog(const ProgramDesc &desc);

// [Doruk-Aksoy] A mod's post-process code made to follow the prolog, its #version lines dropped since the prolog sets the version.
std::string ComputeAdaptPostProcessShader(const std::string &source);

// [Doruk-Aksoy] The console note a renderer prints for a post-process shader it leaves out, with `why`'s lines indented under it.
std::string ComputePostShaderSkipNote(const std::string &renderer, const std::string &name, const std::string &why);

// [Doruk-Aksoy] Whether a shader's code reads the previous frame, which is copied for it only then.
bool ComputeReadsLastInput(const std::string &source);

// [Doruk-Aksoy] The state a level gives one shader, for every player (player -1) or one: on or off (-1 leaves the declaration's), when it went on and when it goes off again (0 never), and the uniforms it set.
struct ShaderEntry
{
	struct Uniform
	{
		std::string name;
		double values[4];
	};
	std::string shader;
	int player = -1;
	int enabled = -1;
	int enabledTic = 0;
	int expiresTic = 0;
	std::vector<Uniform> uniforms;
};

// [Doruk-Aksoy] A level's shader state, its generation bumped by every change so a copy of it is taken only when it changed.
struct ShaderTable
{
	unsigned generation = 0;
	std::vector<ShaderEntry> entries;
};

// [Doruk-Aksoy] Turns `shader` on or off for `player` (-1 everyone) at tic `now`, going off again at `expires` (0 never).
void ComputeSetShaderEnabled(ShaderTable &table, const std::string &shader, int player, bool enabled, int now, int expires);

// [Doruk-Aksoy] Sets a uniform of `shader` for `player` (-1 everyone).
void ComputeSetShaderUniform(ShaderTable &table, const std::string &shader, int player, const std::string &uniform, const double values[4]);

// [Doruk-Aksoy] Whether `shader` runs at tic `now` for the console player, and the tic it went on: the player's own entry over everyone's, over the declaration's Enabled from the level's start.
struct EffectiveEnabled
{
	bool enabled;
	int sinceTic;
};
EffectiveEnabled ComputeEffectiveEnabled(const ShaderTable &table, const std::string &shader, int consolePlayer, bool declared, int now);

// [Doruk-Aksoy] The values a level set for a uniform of `shader`, the console player's over everyone's, or null for the declaration's.
const double *ComputeEffectiveUniform(const ShaderTable &table, const std::string &shader, int consolePlayer, const std::string &uniform);

// [Doruk-Aksoy] What the console's shaderenable sets a shader to: `value` 1 or 0, or -1 for the other of `current`.
bool ComputeConsoleEnable(int value, bool current);

// [Doruk-Aksoy] The shaders of `stage` that run this frame, in GLDEFS order: those of that stage, on, and with their pipeline made.
std::vector<int> ComputePostProcessChain(const std::vector<int> &stages, const std::vector<bool> &enabled, const std::vector<bool> &ready, int stage);

// [Doruk-Aksoy] The pictures a stage's passes read and write: the source, the two ping-pong targets, and the frame's own target.
enum Picture { kPictureSource, kPictureA, kPictureB, kPictureFinal };

struct Pass
{
	int source;
	int target;
};

// [Doruk-Aksoy] `passes` passes, the first reading `first` and each the one before's, alternating between A and B, the last writing the frame's target when `lastToFinal`.
std::vector<Pass> ComputePingPong(int passes, int first, bool lastToFinal);

// [Doruk-Aksoy] The picture a stage leaves its result in: the last pass's target, or `first` when none ran.
int ComputeStageResult(const std::vector<Pass> &passes, int first);

// [Doruk-Aksoy] UZDoom's automatic uniforms, and ours: real seconds, the frame's step, game seconds, and game seconds since the shader went on.
struct Times
{
	float inputTime;
	float inputTimeDelta;
	float inputTimeGame;
	float enabledTime;
};

// [Doruk-Aksoy] The times a shader reads this frame, from the frame clock in ms now and at the last frame, the level's tic and the share of the next one drawn, and the tic it went on.
Times ComputePostProcessTimes(unsigned frameMs, unsigned previousFrameMs, int maptime, double ticFrac, int sinceTic, int ticRate);

// [Doruk-Aksoy] The share of its target a w x h picture covers and its size, as the prolog's fuaPostRegion reads it.
void ComputePostRegion(int w, int h, int targetW, int targetH, float out[4]);

// [Doruk-Aksoy] The prolog's automatic block in floats: the four times, the input's and the previous frame's regions, and the size the pass draws at.
const int kAutoBlockFloats = 16;
void ComputeAutoBlock(const Times &times, const float region[4], const float lastRegion[4], int outW, int outH, float out[kAutoBlockFloats]);

}} // namespace zx::postprocess

#endif // ZX_POSTPROCESS_COMPUTE_H
