/*
** r_opengl.cpp
**
** OpenGL system interface
**
**---------------------------------------------------------------------------
** Copyright 2005 Tim Stump
** Copyright 2005-2013 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/
#include "gl/system/gl_system.h"
#include "tarray.h"
#include "doomtype.h"
#include "m_argv.h"
#include "zstring.h"
#include "version.h"
#include "i_system.h"
#include "v_text.h"
#include "gl/system/gl_interface.h"
#include "gl/system/gl_cvars.h"
#include "doomstat.h"	// [rc4l] for 'developer' (extension-list print gate)

#ifdef _WIN32 // [BB] Detect some kinds of glBegin hooking.
char myGlBeginCharArray[4] = {0,0,0,0};
#endif

#if defined (__unix__) || defined (__APPLE__)
#define PROC void*
#define LPCSTR const char*

#include <SDL.h>
#define wglGetProcAddress(x) (*SDL_GL_GetProcAddress)(x)
#endif


static TArray<FString>  m_Extensions;

RenderContext gl;

int occlusion_type=0;



//==========================================================================
//
// 
//
//==========================================================================

static void CollectExtensions()
{
	const char *extension;

	int max = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &max);

	for(int i = 0; i < max; i++)
	{
		extension = (const char*)glGetStringi(GL_EXTENSIONS, i);
		m_Extensions.Push(FString(extension));
	}

}

//==========================================================================
//
// 
//
//==========================================================================

static bool CheckExtension(const char *ext)
{
	for (unsigned int i = 0; i < m_Extensions.Size(); ++i)
	{
		if (m_Extensions[i].CompareNoCase(ext) == 0) return true;
	}
	return false;
}



//==========================================================================
//
// 
//
//==========================================================================

static void InitContext()
{
	gl.flags=0;

#ifdef _WIN32 // [BB] Detect some kinds of glBegin hooking.
	for ( int i = 0; i < 4; ++i )
		myGlBeginCharArray[i] = reinterpret_cast<char *>(glBegin)[i];
#endif
}

//==========================================================================
//
// 
//
//==========================================================================

void gl_LoadExtensions()
{
	InitContext();

	// [rc4l] Flight 1 (upstream 69af73d9b/94b06900c): one real loader. glewInit resolves every
	// entry point the context offers. This MUST run before CollectExtensions(): on a core
	// profile the extension list comes from glGetStringi, which is a GLEW-resolved pointer
	// (the old glGetString(GL_EXTENSIONS) was statically linked, so the pre-summit ordering
	// never mattered).
	glewExperimental = GL_TRUE;
	GLenum glewErr = glewInit();
	if (glewErr != GLEW_OK)
	{
		I_FatalError("glewInit failed: %s\n", glewGetErrorString(glewErr));
	}
	// glewInit can leave a benign GL_INVALID_ENUM behind on some drivers; clear it.
	while (glGetError() != GL_NO_ERROR) {}

	CollectExtensions();

	const char *version = Args->CheckValue("-glversion");
	if (version == NULL) version = (const char*)glGetString(GL_VERSION);
	else Printf("Emulating OpenGL v %s\n", version);


	// [rc4l] upstream 2925c96b5: GL 3.0 is the floor now that all GL 2.x code is gone.
	if (strcmp(version, "3.0") < 0)
	{
		I_FatalError("Unsupported OpenGL version.\nAt least GL 3.0 is required to run " GAMENAME ".\n");
	}

	// add 0.01 to account for roundoff errors making the number a tad smaller than the actual version
	gl.version = (float)strtod(version, NULL) + 0.01f;
	gl.glslversion = (float)strtod((const char*)glGetString(GL_SHADING_LANGUAGE_VERSION), NULL) + 0.01f;
	gl.vendorstring=(char*)glGetString(GL_VENDOR);
	// [rc4l] Always 4 on the 3.0 floor; kept for the [BB] map_lightmode gates.
	gl.shadermodel = 4;

	if (CheckExtension("GL_ARB_texture_compression")) gl.flags|=RFL_TEXTURE_COMPRESSION;
	if (CheckExtension("GL_EXT_texture_compression_s3tc")) gl.flags|=RFL_TEXTURE_COMPRESSION_S3TC;
	// [rc4l] upstream 0ce6b4067: ARB_buffer_storage requires GL 4.0 per spec, so
	// don't use it when emulating something lower via -glversion.
	if (CheckExtension("GL_ARB_buffer_storage")) gl.flags|=RFL_BUFFER_STORAGE;
	if (CheckExtension("GL_ARB_shader_storage_buffer_object")) gl.flags|=RFL_SHADER_STORAGE_BUFFER;
	// [rc4l] Guaranteed on every GL 3.x context; kept for [BB] call sites.
	gl.flags |= RFL_NPOT_TEXTURE | RFL_OCCLUSION_QUERY;

	glGetIntegerv(GL_MAX_TEXTURE_SIZE,&gl.max_texturesize);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

//==========================================================================
//
// 
//
//==========================================================================

void gl_PrintStartupLog()
{
	Printf ("GL_VENDOR: %s\n", glGetString(GL_VENDOR));
	Printf ("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
	Printf ("GL_VERSION: %s\n", glGetString(GL_VERSION));
	Printf ("GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	// [rc4l] core profile: glGetString(GL_EXTENSIONS) is invalid; enumerate instead ([TDRR] optional via developer CVAR).
	if (developer) { Printf("GL_EXTENSIONS:"); for (unsigned ext_i = 0; ext_i < m_Extensions.Size(); ext_i++) Printf(" %s", m_Extensions[ext_i].GetChars()); Printf("\n"); }
	int v;

	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &v);
	Printf ("Max. texture units: %d\n", v);
	glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &v);
	Printf ("Max. fragment uniforms: %d\n", v);
	if (gl.shadermodel == 4) gl.maxuniforms = v;
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &v);
	Printf ("Max. vertex uniforms: %d\n", v);
	glGetIntegerv(GL_MAX_VARYING_FLOATS, &v);
	Printf ("Max. varying: %d\n", v);
	glGetIntegerv(GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS, &v);
	Printf("Max. combined shader storage blocks: %d\n", v);
	glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &v);
	Printf("Max. vertex shader storage blocks: %d\n", v);

}

//==========================================================================
//
// 
//
//==========================================================================


//==========================================================================
//
// 
//
//==========================================================================

void gl_SetTextureMode(int type)
{
	if (type == TM_MASK)
	{
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);

		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE); 
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_TEXTURE0);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
	}
	else if (type == TM_OPAQUE)
	{
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE0);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE); 
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
	}
	else if (type == TM_INVERSE)
	{
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE0);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_ONE_MINUS_SRC_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE); 
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_TEXTURE0);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
	}
	else // if (type == TM_MODULATE)
	{
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	}
}

//} // extern "C"
