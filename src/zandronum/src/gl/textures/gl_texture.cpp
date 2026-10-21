/*
** Global texture data
**
**---------------------------------------------------------------------------
** Copyright 2004-2009 Christoph Oelckers
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
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
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
#include "c_cvars.h"
#include "c_dispatch.h"

#include "gl/system/gl_interface.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/textures/gl_material.h"
#include "gl/textures/gl_samplers.h"
#include "features/fua-caching/fua_caching.h"

// [rc4l] What GL alone does with textures: its filtering and storage formats, and uploading a level's textures ahead of drawing them; the rest is features/materials/textureinfo.cpp.

//==========================================================================
//
// Texture CVARs
//
//==========================================================================
// [BB] Changed default
CUSTOM_CVAR(Float,r_texture_filter_anisotropic,1.0f,CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOINITCALL)
{
	if (GLRenderer != NULL && GLRenderer->mSamplerManager != NULL) GLRenderer->mSamplerManager->SetTextureFilterMode();
}

// [BB] Changed default
CUSTOM_CVAR(Int, r_texture_filter, 0, CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOINITCALL)
{
	if (self < 0 || self > 5) self=4;
	if (GLRenderer != NULL && GLRenderer->mSamplerManager != NULL) GLRenderer->mSamplerManager->SetTextureFilterMode();
}

CUSTOM_CVAR(Int, gl_texture_format, 0, CVAR_ARCHIVE|CVAR_GLOBALCONFIG|CVAR_NOINITCALL)
{
	// [BB] The number of available texture modes depends on the GPU capabilities.
	// RFL_TEXTURE_COMPRESSION gives us one additional mode and RFL_TEXTURE_COMPRESSION_S3TC
	// another three.
	int numOfAvailableTextureFormat = 4;
	if ( gl.flags & RFL_TEXTURE_COMPRESSION && gl.flags & RFL_TEXTURE_COMPRESSION_S3TC )
		numOfAvailableTextureFormat = 8;
	else if ( gl.flags & RFL_TEXTURE_COMPRESSION )
		numOfAvailableTextureFormat = 5;
	if (self < 0 || self > numOfAvailableTextureFormat-1) self=0;
	FMaterial::FlushAll();   // [rc4l] not through GLRenderer, which a launch without GL does not have
}

CVAR(Bool, gl_precache, false, CVAR_ARCHIVE)

TexFilter_s TexFilter[]={
	{GL_NEAREST,					GL_NEAREST,		false},
	{GL_NEAREST_MIPMAP_NEAREST,		GL_NEAREST,		true},
	{GL_LINEAR,						GL_LINEAR,		false},
	{GL_LINEAR_MIPMAP_NEAREST,		GL_LINEAR,		true},
	{GL_LINEAR_MIPMAP_LINEAR,		GL_LINEAR,		true},
	{GL_NEAREST_MIPMAP_LINEAR,		GL_NEAREST,		true},
};

int TexFormat[]={
	GL_RGBA8,
	GL_RGB5_A1,
	GL_RGBA4,
	GL_RGBA2,
	// [BB] Added compressed texture formats.
	GL_COMPRESSED_RGBA_ARB,
	GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
	GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
	GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
};



//==========================================================================
//
// Precaches a GL texture
//
//==========================================================================

void FTexture::PrecacheGL(int cache)
{
	// [ForkUnderA] cl_fua_caching arms the upload path; gl_precache kept as
	// the legacy opt-in.
	if (gl_precache || FUA_CachingMode() > 0)
	{
		if (cache & (FTextureManager::HIT_Wall | FTextureManager::HIT_Flat | FTextureManager::HIT_Sky))
		{
			FMaterial * gltex = FMaterial::ValidateTexture(this, false);
			if (gltex) gl_PrecacheMaterial(gltex);
		}
		if (cache & FTextureManager::HIT_Sprite)
		{
			FMaterial * gltex = FMaterial::ValidateTexture(this, true);
			if (gltex) gl_PrecacheMaterial(gltex);
		}
		gl_info.precacheTime = TexMan.precacheTime;
	}
}

//==========================================================================
//
// Precaches a GL texture
//
//==========================================================================

void FTexture::UncacheGL()
{
	if (gl_info.precacheTime != TexMan.precacheTime)
	{
		if (gl_info.Material[0]) gl_info.Material[0]->Clean(true);
		if (gl_info.Material[1]) gl_info.Material[1]->Clean(true);
		gl_info.precacheTime = 0;
	}
}

