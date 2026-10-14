/*
** gl_material.cpp
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
#include "r_utility.h"
#include "templates.h"

#include "gl/system/gl_interface.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/textures/gl_material.h"
#include "features/materials/translationpalette.h"
#include "gl/textures/gl_samplers.h"

// [rc4l] FMaterial and FTexturePixels live in features/materials; what stays here makes and binds GL's copies of their pixels.

//===========================================================================
//
//	Create hardware texture for world use
//
//===========================================================================

static FHardwareTexture *CreateHwTexture(FTexturePixels *pixels)
{
	FTexture *tex = pixels->tex;
	if (tex->UseType==FTexture::TEX_Null) return NULL;		// Cannot register a NULL texture
	if (pixels->mHwTexture == NULL)
	{
		pixels->mHwTexture = new FHardwareTexture(tex->GetWidth() + pixels->bExpandFlag*2, tex->GetHeight() + pixels->bExpandFlag*2, tex->gl_info.bNoCompress);
	}
	// [rc4l] GL is the only renderer that hangs a copy here.
	return static_cast<FHardwareTexture *>(pixels->mHwTexture);
}

//===========================================================================
//
//	Binds a texture to the renderer
//
//===========================================================================

static const FHardwareTexture *BindPixels(FTexturePixels *pixels, int texunit, int clampmode, int translation, FTexture *hirescheck)
{
	FTexture *tex = pixels->tex;

	if (translation <= 0) translation = -translation;
	else translation = FTranslationPalette::GetInternalTranslation(translation);

	bool needmipmap = (clampmode <= CLAMP_XY);

	FHardwareTexture *hwtex = CreateHwTexture(pixels);

	if (hwtex)
	{
		// Texture has become invalid
		if ((!tex->bHasCanvas && !tex->bWarped) && tex->CheckModified())
		{
			pixels->Clean(true);
			hwtex = CreateHwTexture(pixels);
		}

		// Bind it to the system.
		if (!hwtex->Bind(texunit, translation, needmipmap))
		{

			int w=0, h=0;

			// Create this texture
			unsigned char * buffer = NULL;

			if (!tex->bHasCanvas)
			{
				buffer = pixels->CreateTexBuffer(translation, w, h, hirescheck);
				tex->ProcessData(buffer, w, h, false);
			}
			if (!hwtex->CreateTexture(buffer, w, h, texunit, needmipmap, translation))
			{
				// could not create texture
				delete[] buffer;
				return NULL;
			}
			delete[] buffer;
		}

		if (tex->bHasCanvas) static_cast<FCanvasTexture*>(tex)->NeedUpdate();
		GLRenderer->mSamplerManager->Bind(texunit, clampmode);
		return hwtex;
	}
	return NULL;
}

// [rc4l] Bind a material's layers to GL's texture units, uploading their pixels the first time.

static FMaterial *last;
static int lastclamp;
static int lasttrans;


void gl_BindMaterial(FMaterial *mat, int clampmode, int translation)
{
	// avoid rebinding the same texture multiple times.
	if (mat == last && lastclamp == clampmode && translation == lasttrans) return;
	last = mat;
	lastclamp = clampmode;
	lasttrans = translation;

	FTexture *tex = mat->tex;
	bool allowhires = tex->xScale == FRACUNIT && tex->yScale == FRACUNIT && clampmode <= CLAMP_XY && !mat->IsExpanded();

	if (tex->bHasCanvas) clampmode = CLAMP_CAMTEX;
	else if (tex->bWarped && clampmode <= CLAMP_XY) clampmode = CLAMP_NONE;

	const FHardwareTexture *gltexture = BindPixels(mat->GetBaseLayer(), 0, clampmode, translation, allowhires? tex:NULL);
	if (gltexture != NULL)
	{
		for(int i=1;i<mat->GetLayers();i++)
		{
			BindPixels(mat->GetLayerPixels(i), i, clampmode, 0, NULL);
		}
	}
}


//===========================================================================
//
//
//
//===========================================================================
void gl_PrecacheMaterial(FMaterial *mat)
{
	gl_BindMaterial(mat, 0, 0);
}

//===========================================================================
//
//
//
//===========================================================================

void gl_BindMaterialToFrameBuffer(FMaterial *mat)
{
	FTexturePixels *base = mat->GetBaseLayer();
	if (base->mHwTexture == NULL)
	{
		// must create the hardware texture first
		BindPixels(base, 0, 0, 0, NULL);
		FHardwareTexture::Unbind(0);
		gl_ClearLastMaterial();
	}
	static_cast<FHardwareTexture *>(base->mHwTexture)->BindToFrameBuffer();
}

void gl_ClearLastMaterial()
{
	last = NULL;
}
