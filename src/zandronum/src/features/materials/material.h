/*
** material.h
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

// [rc4l] GL's FMaterial and the CPU half of its FGLTexture, moved out of gl/textures/gl_material.h so any renderer reads a texture's layout and pixels; GL keeps only binding them.

#ifndef ZX_MATERIAL_H
#define ZX_MATERIAL_H

#include "m_fixed.h"
#include "textures/textures.h"
#include "features/materials/hardwaretexture.h"
#include "features/materials/computation/texcoord_compute.h"   // [rc4l] FTexCoordInfo
#include "features/materials/computation/materiallayout_compute.h"

// [rc4l] A texture's pixels on the CPU, composed, translated, hires-replaced and upsampled, with the renderer's copy of them hanging off it.

class FTexturePixels
{
public:
	FTexture * tex;
	FTexture * hirestexture;
	char bIsTransparent;
	int HiresLump;
	bool bExpandFlag;
	IHardwareTexture *mHwTexture;   // [rc4l] the renderer's copy, made by whichever renderer binds it

private:
	bool bHasColorkey;		// only for hires

	unsigned char * LoadHiresTexture(FTexture *hirescheck, int *width, int *height);

public:
	FTexturePixels(FTexture * tx, bool expandpatches);
	~FTexturePixels();

	unsigned char * CreateTexBuffer(int translation, int & w, int & h, FTexture *hirescheck, bool createexpanded = true);

	void Clean(bool all);
};

// [rc4l] A texture as it is drawn: its layers, its size and offsets, and where a sprite of it sits.

class FMaterial
{
	struct FTextureLayer
	{
		FTexture *texture;
		bool animated;
	};

	static TArray<FMaterial *> mMaterials;

	FTexturePixels *mBaseLayer;
	TArray<FTextureLayer> mTextureLayers;
	int mShaderIndex;
	zx::materials::MaterialLayout mLayout;
	bool mExpanded;

	static FTexturePixels * ValidateSysTexture(FTexture * tex, bool expand);
	bool TrimBorders(int *rect);

public:
	FTexture *tex;

	FMaterial(FTexture *tex, bool forceexpand);
	~FMaterial();
	bool isMasked() const
	{
		return !!mBaseLayer->tex->bMasked;
	}

	int GetLayers() const
	{
		return mTextureLayers.Size() + 1;
	}

	// [rc4l] The shader GL draws it with: 0 plain, 1-2 a warp, 3 a brightmap, or a GLDEFS hardware shader.
	int GetShaderIndex() const { return mShaderIndex; }
	bool IsExpanded() const { return mExpanded; }
	FTexturePixels *GetBaseLayer() const { return mBaseLayer; }
	// [rc4l] The pixels of layer `layer` (1 up to GetLayers()-1) as it shows now, an animated layer through its animation.
	FTexturePixels *GetLayerPixels(int layer) const;

	unsigned char * CreateTexBuffer(int translation, int & w, int & h, bool allowhires=true, bool createexpanded = true) const
	{
		return mBaseLayer->CreateTexBuffer(translation, w, h, allowhires? tex : NULL, createexpanded);
	}

	void Clean(bool f)
	{
		mBaseLayer->Clean(f);
	}

	// Patch drawing utilities

	void GetSpriteRect(FloatRect * r) const
	{
		r->left = mLayout.spriteLeft;
		r->top = mLayout.spriteTop;
		r->width = mLayout.spriteWidth;
		r->height = mLayout.spriteHeight;
	}

	void GetTexCoordInfo(FTexCoordInfo *tci, fixed_t x, fixed_t y) const;

	// This is scaled size in integer units as needed by walls and flats
	int TextureHeight() const { return mLayout.renderHeight; }
	int TextureWidth() const { return mLayout.renderWidth; }

	int GetAreas(FloatRect **pAreas) const;

	int GetWidth() const
	{
		return mLayout.width;
	}

	int GetHeight() const
	{
		return mLayout.height;
	}

	int GetLeftOffset() const
	{
		return mLayout.leftOffset;
	}

	int GetTopOffset() const
	{
		return mLayout.topOffset;
	}

	int GetScaledLeftOffset() const
	{
		// [rc4l] fixed64: DivScale16 on the widened type returns zx::Fixed; the result
		// here is a texel count, so convert explicitly.
		return (int)DivScale16(mLayout.leftOffset, tex->xScale);
	}

	int GetScaledTopOffset() const
	{
		return (int)DivScale16(mLayout.topOffset, tex->yScale);
	}

	float GetScaledLeftOffsetFloat() const
	{
		return mLayout.leftOffset / FIXED2FLOAT(tex->xScale);
	}

	float GetScaledTopOffsetFloat() const
	{
		return mLayout.topOffset / FIXED2FLOAT(tex->yScale);
	}

	// This is scaled size in floating point as needed by sprites
	float GetScaledWidthFloat() const
	{
		return mLayout.width / FIXED2FLOAT(tex->xScale);
	}

	float GetScaledHeightFloat() const
	{
		return mLayout.height / FIXED2FLOAT(tex->yScale);
	}

	// Get right/bottom UV coordinates for patch drawing
	float GetUL() const { return 0; }
	float GetVT() const { return 0; }
	float GetUR() const { return 1; }
	float GetVB() const { return 1; }
	float GetU(float upix) const { return upix/(float)mLayout.width; }
	float GetV(float vpix) const { return vpix/(float)mLayout.height; }

	float GetSpriteUL() const { return mLayout.spriteU[0]; }
	float GetSpriteVT() const { return mLayout.spriteV[0]; }
	float GetSpriteUR() const { return mLayout.spriteU[1]; }
	float GetSpriteVB() const { return mLayout.spriteV[1]; }

	bool GetTransparent() const;

	static void FlushAll();
	static FMaterial *ValidateTexture(FTexture * tex, bool expand);
	// [rc4l] The texture's material if it is made, without making it; the texture must be one that has a material.
	static FMaterial *Existing(FTexture *tex, bool expand);
	static FMaterial *ValidateTexture(FTextureID no, bool expand, bool trans);
	// [rc4l] A texture's base layer as true-colour pixels the caller deletes, read on the CPU so it needs no GL; NULL when it has none.
	static unsigned char *TextureBuffer(FTexture *tex, int &w, int &h);
};

#endif // ZX_MATERIAL_H
