/*
** material.cpp
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

// [rc4l] GL's FMaterial and the CPU half of its FGLTexture, moved from gl/textures/gl_material.cpp; GL's binding of them stays there.

#include "w_wad.h"
#include "templates.h"
#include "c_cvars.h"
#include "features/materials/material.h"
#include "features/materials/materials.h"   // [rc4l] NoteTexturesFlushed
#include "features/materials/textureinfo.h"   // [rc4l] hires lookup, upsampling
#include "features/materials/texturebitmap.h"
#include "features/hwshaders/computation/hwshader_compute.h"   // [rc4l] kFirstUserShader

EXTERN_CVAR(Bool, r_texture_usehires)

using zx::hwshaders::kFirstUserShader;

//===========================================================================
//
// Constructor
//
//===========================================================================
FTexturePixels::FTexturePixels(FTexture * tx, bool expandpatches)
{
	assert(tx->gl_info.SystemTexture[expandpatches] == NULL);
	tex = tx;

	mHwTexture = NULL;
	HiresLump = -1;
	hirestexture = NULL;
	bHasColorkey = false;
	bIsTransparent = -1;
	bExpandFlag = expandpatches;
	tex->gl_info.SystemTexture[expandpatches] = this;
}

//===========================================================================
//
// Destructor
//
//===========================================================================

FTexturePixels::~FTexturePixels()
{
	Clean(true);
	if (hirestexture) delete hirestexture;
}

//==========================================================================
//
// Checks for the presence of a hires texture replacement and loads it
//
//==========================================================================
unsigned char *FTexturePixels::LoadHiresTexture(FTexture *tex, int *width, int *height)
{
	if (bExpandFlag) return NULL;	// doesn't work for expanded textures

	if (HiresLump==-1)
	{
		bHasColorkey = false;
		HiresLump = CheckDDPK3(tex);
		if (HiresLump < 0) HiresLump = CheckExternalFile(tex, bHasColorkey);

		if (HiresLump >=0)
		{
			hirestexture = FTexture::CreateTexture(HiresLump, FTexture::TEX_Any);
		}
	}
	if (hirestexture != NULL)
	{
		int w=hirestexture->GetWidth();
		int h=hirestexture->GetHeight();

		unsigned char * buffer=new unsigned char[w*(h+1)*4];
		memset(buffer, 0, w * (h+1) * 4);

		FTextureBitmap bmp(buffer, w*4, w, h);

		int trans = hirestexture->CopyTrueColorPixels(&bmp, 0, 0);
		hirestexture->CheckTrans(buffer, w*h, trans);
		bIsTransparent = hirestexture->gl_info.mIsTransparent;

		if (bHasColorkey)
		{
			// This is a crappy Doomsday color keyed image
			// We have to remove the key manually. :(
			DWORD * dwdata=(DWORD*)buffer;
			for (int i=(w*h);i>0;i--)
			{
				if (dwdata[i]==0xffffff00 || dwdata[i]==0xffff00ff) dwdata[i]=0;
			}
		}
		*width = w;
		*height = h;
		return buffer;
	}
	return NULL;
}

//===========================================================================
//
//	Deletes all allocated resources
//
//===========================================================================

void FTexturePixels::Clean(bool all)
{
	if (mHwTexture)
	{
		if (!all) mHwTexture->Clean(false);
		else
		{
			delete mHwTexture;
			mHwTexture = NULL;
		}
	}
}

//===========================================================================
//
//	Initializes the buffer for the texture data
//
//===========================================================================

unsigned char * FTexturePixels::CreateTexBuffer(int translation, int & w, int & h, FTexture *hirescheck, bool createexpanded)
{
	unsigned char * buffer;
	int W, H;


	// Textures that are already scaled in the texture lump will not get replaced
	// by hires textures
	if (r_texture_usehires && hirescheck != NULL)
	{
		buffer = LoadHiresTexture (hirescheck, &w, &h);
		if (buffer)
		{
			return buffer;
		}
	}

	int exx = bExpandFlag && createexpanded;

	W = w = tex->GetWidth() + 2 * exx;
	H = h = tex->GetHeight() + 2 * exx;


	buffer=new unsigned char[W*(H+1)*4];
	memset(buffer, 0, W * (H+1) * 4);

	FTextureBitmap bmp(buffer, W*4, W, H);
	bmp.SetTranslationInfo(translation);

	if (tex->bComplex)
	{
		FBitmap imgCreate;

		// The texture contains special processing so it must be composited using the
		// base bitmap class and then be converted as a whole.
		if (imgCreate.Create(W, H))
		{
			memset(imgCreate.GetPixels(), 0, W * H * 4);
			int trans = tex->CopyTrueColorPixels(&imgCreate, exx, exx);
			bmp.CopyPixelDataRGB(0, 0, imgCreate.GetPixels(), W, H, 4, W * 4, 0, CF_BGRA);
			tex->CheckTrans(buffer, W*H, trans);
			bIsTransparent = tex->gl_info.mIsTransparent;
		}
	}
	else if (translation<=0)
	{
		int trans = tex->CopyTrueColorPixels(&bmp, exx, exx);
		tex->CheckTrans(buffer, W*H, trans);
		bIsTransparent = tex->gl_info.mIsTransparent;
	}
	else
	{
		// When using translations everything must be mapped to the base palette.
		// Since FTexture's method is doing exactly that by calling GetPixels let's use that here
		// to do all the dirty work for us. ;)
		tex->FTexture::CopyTrueColorPixels(&bmp, exx, exx);
		bIsTransparent = 0;
	}

	// if we just want the texture for some checks there's no need for upsampling.
	if (!createexpanded) return buffer;

	// [BB] The hqnx upsampling (not the scaleN one) destroys partial transparency, don't upsamle textures using it.
	// [BB] Potentially upsample the buffer.
	return gl_CreateUpsampledTextureBuffer ( tex, buffer, W, H, w, h, !!bIsTransparent);
}

//===========================================================================
//
//
//
//===========================================================================
FTexturePixels * FMaterial::ValidateSysTexture(FTexture * tex, bool expand)
{
	if (tex	&& tex->UseType!=FTexture::TEX_Null)
	{
		FTexturePixels *gltex = tex->gl_info.SystemTexture[expand];
		if (gltex == NULL)
		{
			gltex = new FTexturePixels(tex, expand);
		}
		return gltex;
	}
	return NULL;
}

//===========================================================================
//
// Constructor
//
//===========================================================================
TArray<FMaterial *> FMaterial::mMaterials;

FMaterial::FMaterial(FTexture * tx, bool expanded)
{
	mShaderIndex = 0;
	tex = tx;

	if (tx->bWarped)
	{
		mShaderIndex = tx->bWarped;
		tx->gl_info.shaderspeed = static_cast<FWarpTexture*>(tx)->GetSpeed();
	}
	else if (tx->bHasCanvas)
	{
	}
	else
	{
		if (tx->gl_info.shaderindex >= kFirstUserShader)
		{
			mShaderIndex = tx->gl_info.shaderindex;
		}
		else
		{
			tx->CreateDefaultBrightmap();
			if (tx->gl_info.Brightmap != NULL)
			{
				ValidateSysTexture(tx->gl_info.Brightmap, expanded);
				FTextureLayer layer = {tx->gl_info.Brightmap, false};
				mTextureLayers.Push(layer);
				mShaderIndex = 3;
			}
		}
	}
	mBaseLayer = ValidateSysTexture(tx, expanded);

	FTexture *basetex = tx->GetRedirect(false);
	// allow the redirect only if the textute is not expanded or the scale matches.
	if (!expanded || (tx->xScale == basetex->xScale && tx->yScale == basetex->yScale))
	{
		mBaseLayer = ValidateSysTexture(basetex, expanded);
	}

	// [rc4l] A sprite's frame and trim are worked out from its unframed picture.
	int trim[4] = { 0, 0, 0, 0 };
	const bool trimmed = expanded && TrimBorders(trim);
	const zx::materials::MaterialSource src = { tx->GetWidth(), tx->GetHeight(), tx->LeftOffset, tx->TopOffset,
		tx->GetScaledWidth(), tx->GetScaledHeight(), FIXED2FLOAT(tx->xScale), FIXED2FLOAT(tx->yScale) };
	mLayout = zx::materials::ComputeMaterialLayout(src, expanded, trimmed, trim);

	mTextureLayers.ShrinkToFit();
	mMaterials.Push(this);
	tx->gl_info.Material[expanded] = this;
	if (tx->bHasCanvas) tx->gl_info.mIsTransparent = 0;
	mExpanded = expanded;
}

//===========================================================================
//
// Destructor
//
//===========================================================================

FMaterial::~FMaterial()
{
	for(unsigned i=0;i<mMaterials.Size();i++)
	{
		if (mMaterials[i]==this)
		{
			mMaterials.Delete(i);
			break;
		}
	}

}

//===========================================================================
//
//	Finds gaps in the texture which can be skipped by the renderer
//  This was mainly added to speed up one area in E4M6 of 007LTSD
//
//===========================================================================

bool FMaterial::TrimBorders(int *rect)
{
	int w;
	int h;

	unsigned char *buffer = CreateTexBuffer(0, w, h, false, false);

	if (buffer == NULL)
	{
		return false;
	}
	// [rc4l] External hires replacements cannot be trimmed.
	const bool trimmed = w == tex->GetWidth() && h == tex->GetHeight() && zx::materials::ComputeTrimBorders(buffer, w, h, rect);
	delete [] buffer;
	return trimmed;
}

//===========================================================================
//
// The pixels of an extra layer, an animated one through its animation
//
//===========================================================================

FTexturePixels *FMaterial::GetLayerPixels(int layer) const
{
	const FTextureLayer &l = mTextureLayers[layer - 1];
	FTexture *t = l.texture;
	if (l.animated)
	{
		t = TexMan(l.texture->id);
		ValidateSysTexture(t, mExpanded);
	}
	return t->gl_info.SystemTexture[mExpanded];
}

//===========================================================================
//
// Retrieve texture coordinate info for per-wall scaling
//
//===========================================================================

void FMaterial::GetTexCoordInfo(FTexCoordInfo *tci, fixed_t x, fixed_t y) const
{
	const TexCoordSource src = { mLayout.width, mLayout.height, mLayout.renderWidth, mLayout.renderHeight,
		tex->xScale, tex->yScale, !!tex->bHasCanvas, !!tex->bWorldPanning };
	ComputeTexCoordInfo(src, x, y, tci);
}

//===========================================================================
//
//
//
//===========================================================================

int FMaterial::GetAreas(FloatRect **pAreas) const
{
	if (mShaderIndex == 0)	// texture splitting can only be done if there's no attached effects
	{
		FTexture *tex = mBaseLayer->tex;
		*pAreas = tex->gl_info.areas;
		return tex->gl_info.areacount;
	}
	else
	{
		return 0;
	}
}

//===========================================================================
//
// Whether the texture has partly see-through texels; the first ask decodes it
//
//===========================================================================

bool FMaterial::GetTransparent() const
{
	if (mBaseLayer->bIsTransparent == -1)
	{
		if (!mBaseLayer->tex->bHasCanvas)
		{
			int w, h;
			unsigned char *buffer = CreateTexBuffer(0, w, h);
			delete [] buffer;
		}
		else
		{
			mBaseLayer->bIsTransparent = 0;
		}
	}
	return !!mBaseLayer->bIsTransparent;
}

//==========================================================================
//
// Gets a texture from the texture manager and checks its validity for
// GL rendering.
//
//==========================================================================

FMaterial * FMaterial::ValidateTexture(FTexture * tex, bool expand)
{
again:
	if (tex	&& tex->UseType!=FTexture::TEX_Null)
	{
		if (tex->gl_info.bNoExpand) expand = false;

		FMaterial *gltex = tex->gl_info.Material[expand];
		if (gltex == NULL)
		{
			if (expand)
			{
				if (tex->bWarped || tex->bHasCanvas || tex->gl_info.shaderindex >= kFirstUserShader)
				{
					tex->gl_info.bNoExpand = true;
					goto again;
				}
				if (tex->gl_info.Brightmap != NULL &&
					(tex->GetWidth() != tex->gl_info.Brightmap->GetWidth() ||
					tex->GetHeight() != tex->gl_info.Brightmap->GetHeight())
					)
				{
					// do not expand if the brightmap's size differs.
					tex->gl_info.bNoExpand = true;
					goto again;
				}
			}
			gltex = new FMaterial(tex, expand);
		}
		return gltex;
	}
	return NULL;
}

unsigned char *FMaterial::TextureBuffer(FTexture *tex, int &w, int &h)
{
	FMaterial *gltex = ValidateTexture(tex, false);
	return gltex != NULL ? gltex->CreateTexBuffer(0, w, h) : NULL;
}

FMaterial * FMaterial::ValidateTexture(FTextureID no, bool expand, bool translate)
{
	return ValidateTexture(translate? TexMan(no) : TexMan[no], expand);
}


//==========================================================================
//
// Flushes all hardware dependent data
//
//==========================================================================

void FMaterial::FlushAll()
{
	zx::materials::NoteTexturesFlushed();   // [rc4l] the backend's copies go too, at its next frame
	for(int i=mMaterials.Size()-1;i>=0;i--)
	{
		mMaterials[i]->Clean(true);
	}
	// This is for shader layers. All shader layers must be managed by the texture manager
	// so this will catch everything.
	for(int i=TexMan.NumTextures()-1;i>=0;i--)
	{
		for (int j = 0; j < 2; j++)
		{
			FTexturePixels *gltex = TexMan.ByIndex(i)->gl_info.SystemTexture[j];
			if (gltex != NULL) gltex->Clean(true);
		}
	}
}
