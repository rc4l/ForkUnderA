// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] GLDEFS post-process shaders (HardwareShader PostProcess): the declarations GLDEFS makes, the console's commands for them, and what the renderer asks of them each frame.

#ifndef ZX_POSTPROCESS_H
#define ZX_POSTPROCESS_H

#include "features/postprocess/computation/postprocess_compute.h"

#include <array>
#include <string>
#include <vector>

class FScanner;
class FBaseCVar;

namespace zx { namespace postprocess {

// [Doruk-Aksoy] One GLDEFS post-process shader: its name, lump and stage, whether it starts on, its uniforms laid out with their defaults and cvars, and its extra textures.
struct ShaderDecl
{
	std::string name;
	std::string lump;
	int stage = kStageScene;
	bool enabled = false;
	std::vector<UniformField> uniforms;
	std::vector<std::array<double, 4>> defaults;
	std::vector<FBaseCVar *> cvars;
	unsigned blockSize = 16;
	std::vector<std::string> textureNames;
	std::vector<std::string> textureFiles;
};

// [Doruk-Aksoy] Drops every declaration, as GLDEFS is read again on a restart.
void ClearPostProcessShaders();

// [Doruk-Aksoy] One `HardwareShader PostProcess <target> { ... }` block, the scanner past `PostProcess`.
void ParsePostProcessShader(FScanner &sc);

int PostShaderCount();
const ShaderDecl &PostShader(int i);
// [Doruk-Aksoy] Bumped whenever the declarations change, so a renderer makes its programs again.
unsigned PostShadersGeneration();

// [Doruk-Aksoy] Says once at launch that a GL window draws without the declared shaders.
void NotePostProcessRenderer();

// [Doruk-Aksoy] Whether post-process shaders run at all (gl_custompost).
bool PostProcessAllowed();

// [Doruk-Aksoy] Whether shader i runs this frame for the console player, from the render copy of the level, and the tic it went on.
EffectiveEnabled PostShaderEnabledNow(int i);

// [Doruk-Aksoy] Shader i's uniform block as it stands this frame: the level's values over its cvars' over its defaults.
void PostShaderBlock(int i, unsigned char *block);

}} // namespace zx::postprocess

#endif // ZX_POSTPROCESS_H
