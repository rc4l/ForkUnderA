// [rc4l] Shader gamma present pass -- see shadergamma.h and README.md.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gl/system/gl_system.h"
#include "w_wad.h"
#include "v_video.h"
#include "c_cvars.h"
#include "gl/system/gl_interface.h"
#include "features/shader-gamma/shadergamma.h"
#include "features/shader-gamma/computation/gamma_uniforms_compute.h"

EXTERN_CVAR (Float, Gamma)
EXTERN_CVAR (Float, vid_brightness)
EXTERN_CVAR (Float, vid_contrast)

namespace zx
{

namespace
{

unsigned int s_program = 0;
unsigned int s_vao = 0;
unsigned int s_vbo = 0;
int s_locInputTexture = -1;
int s_locInvGamma = -1;
int s_locContrast = -1;
int s_locBrightness = -1;
bool s_tried = false;
bool s_ready = false;

// [rc4l] Compile one stage, reporting the driver's log rather than a bare failure -- a silent
// shader failure here means the screen never gets presented, which is the worst thing to debug
// blind.
unsigned int CompileStage(unsigned int type, const char *source, const char *what)
{
	const unsigned int handle = glCreateShader(type);
	glShaderSource(handle, 1, &source, NULL);
	glCompileShader(handle);

	int ok = 0;
	glGetShaderiv(handle, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[2048];
		int len = 0;
		glGetShaderInfoLog(handle, sizeof(log) - 1, &len, log);
		log[len < (int)sizeof(log) ? (len < 0 ? 0 : len) : (int)sizeof(log) - 1] = 0;
		Printf(TEXTCOLOR_RED "shader-gamma: %s stage failed to compile:\n%s\n", what, log);
		glDeleteShader(handle);
		return 0;
	}
	return handle;
}

// [rc4l] Read a shader lump. Returns false (rather than I_Error) so a missing lump degrades to the
// plain blit instead of killing the engine -- an out-of-date pk3 should cost you gamma, not the game.
bool ReadLumpText(const char *name, FString &out)
{
	const int lump = Wads.CheckNumForFullName(name);
	if (lump == -1)
	{
		Printf(TEXTCOLOR_RED "shader-gamma: missing lump '%s'\n", name);
		return false;
	}
	FMemLump data = Wads.ReadLump(lump);
	out = FString(static_cast<const char *>(data.GetMem()), data.GetSize());
	return true;
}

} // namespace

bool ShaderGammaInit()
{
	if (s_tried)
		return s_ready;
	s_tried = true;

	FString vpText, fpText;
	if (!ReadLumpText("shaders/glsl/present.vp", vpText) ||
		!ReadLumpText("shaders/glsl/present.fp", fpText))
		return false;

	// [rc4l] The version directive is prepended here, matching how gl_shader.cpp builds the main
	// shaders -- the lumps stay version-agnostic so they are not pinned to one GL profile.
	FString vpSrc, fpSrc;
	vpSrc.Format("#version 330 core\n%s", vpText.GetChars());
	fpSrc.Format("#version 330 core\n%s", fpText.GetChars());

	const unsigned int vs = CompileStage(GL_VERTEX_SHADER, vpSrc.GetChars(), "vertex");
	if (vs == 0) return false;
	const unsigned int fs = CompileStage(GL_FRAGMENT_SHADER, fpSrc.GetChars(), "fragment");
	if (fs == 0) { glDeleteShader(vs); return false; }

	s_program = glCreateProgram();
	glAttachShader(s_program, vs);
	glAttachShader(s_program, fs);
	// [rc4l] Bind attribute locations before linking; the lumps declare plain `in` variables with
	// no layout qualifier so they stay readable on both core and compatibility profiles.
	glBindAttribLocation(s_program, 0, "aPosition");
	glBindAttribLocation(s_program, 1, "aTexCoord");
	glLinkProgram(s_program);

	int linked = 0;
	glGetProgramiv(s_program, GL_LINK_STATUS, &linked);
	glDeleteShader(vs);
	glDeleteShader(fs);
	if (!linked)
	{
		char log[2048];
		int len = 0;
		glGetProgramInfoLog(s_program, sizeof(log) - 1, &len, log);
		log[len < (int)sizeof(log) ? (len < 0 ? 0 : len) : (int)sizeof(log) - 1] = 0;
		Printf(TEXTCOLOR_RED "shader-gamma: present program failed to link:\n%s\n", log);
		glDeleteProgram(s_program);
		s_program = 0;
		return false;
	}

	s_locInputTexture = glGetUniformLocation(s_program, "InputTexture");
	s_locInvGamma = glGetUniformLocation(s_program, "InvGamma");
	s_locContrast = glGetUniformLocation(s_program, "Contrast");
	s_locBrightness = glGetUniformLocation(s_program, "Brightness");

	// [rc4l] Two triangles covering clip space, with texcoords. A VAO is required on a core
	// profile -- there is no default one to fall back on.
	static const float verts[] = {
		// x      y     u     v
		-1.0f, -1.0f, 0.0f, 0.0f,
		 1.0f, -1.0f, 1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f, 1.0f,
		 1.0f,  1.0f, 1.0f, 1.0f,
	};
	glGenVertexArrays(1, &s_vao);
	glGenBuffers(1, &s_vbo);
	glBindVertexArray(s_vao);
	glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
						  (const void *)(2 * sizeof(float)));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	s_ready = true;
	// [rc4l] Say so at startup, next to the other GL capability lines. Whether gamma is being done
	// in the shader or by the OS ramp changes where a brightness complaint has to be investigated
	// -- and if this line is absent, the hardware ramp is live and alt-tab will tint the desktop.
	Printf("Shader gamma enabled.\n");
	return true;
}

void ShaderGammaShutdown()
{
	if (s_vbo) { glDeleteBuffers(1, &s_vbo); s_vbo = 0; }
	if (s_vao) { glDeleteVertexArrays(1, &s_vao); s_vao = 0; }
	if (s_program) { glDeleteProgram(s_program); s_program = 0; }
	s_ready = false;
	s_tried = false;
}

bool ShaderGammaReady()
{
	return s_ready;
}

void ShaderGammaPresent(unsigned int sceneTexture, int destW, int destH)
{
	if (!s_ready)
		return;

	const GammaUniforms u = ComputeGammaUniforms(Gamma, vid_contrast, vid_brightness);

	// [rc4l] The present must land the scene on screen unconditionally, so anything that could
	// discard or blend the quad away is turned off for the duration.
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);

	glViewport(0, 0, destW, destH);
	glUseProgram(s_program);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sceneTexture);
	if (s_locInputTexture >= 0) glUniform1i(s_locInputTexture, 0);
	if (s_locInvGamma >= 0) glUniform1f(s_locInvGamma, u.invGamma);
	if (s_locContrast >= 0) glUniform1f(s_locContrast, u.contrast);
	if (s_locBrightness >= 0) glUniform1f(s_locBrightness, u.brightness);

	glBindVertexArray(s_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);

	glUseProgram(0);
	glDepthMask(GL_TRUE);
}

} // namespace zx
