// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/materials/materials.h"

#include "textures/textures.h"
#include "gl/textures/gl_material.h"
#include "gl/textures/gl_translate.h"   // GLTranslationPalette
#include "gl/system/gl_interface.h"     // gl.flags, whether trimmed sprite coordinates are usable

namespace zx { namespace materials {

static const FMaterial *Mat(MaterialRef m) { return (const FMaterial *)m; }

bool DescribeMaterial(MaterialRef m, MaterialInfo &info)
{
	if (m == NULL || Mat(m)->tex == NULL) return false;
	FTexture *tex = Mat(m)->tex;
	info.name = tex->Name.GetChars();
	info.textureIndex = tex->id.GetIndex();
	info.width = tex->GetWidth();
	info.height = tex->GetHeight();
	info.renderWidth = Mat(m)->TextureWidth();
	info.renderHeight = Mat(m)->TextureHeight();
	info.masked = tex->bMasked;
	info.canvas = tex->bHasCanvas;
	info.warp = tex->bWarped;
	info.complex = tex->bComplex;
	info.basePalette = tex->UseBasePalette();
	info.brightmap = tex->gl_info.Brightmap != NULL;
	info.shaderSpeed = tex->gl_info.shaderspeed;
	return true;
}

MaterialRef MaterialForTexture(const void *texture, bool expand)
{
	return texture ? FMaterial::ValidateTexture((FTexture *)texture, expand) : NULL;
}

bool MaterialSpriteFrame(MaterialRef m, SpriteFrameInfo &info)
{
	if (m == NULL || Mat(m)->tex == NULL) return false;
	FMaterial *mat = (FMaterial *)m;
	FloatRect r;
	mat->GetSpriteRect(&r);
	info.left = r.left; info.top = r.top; info.width = r.width; info.height = r.height;
	// [rc4l] Trimming needs non-power-of-two textures, as GLSprite::Process checks.
	if (gl.flags & RFL_NPOT_TEXTURE)
	{
		info.ul = mat->GetSpriteUL(); info.ur = mat->GetSpriteUR();
		info.vt = mat->GetSpriteVT(); info.vb = mat->GetSpriteVB();
	}
	else
	{
		info.ul = mat->GetUL(); info.ur = mat->GetUR();
		info.vt = mat->GetVT(); info.vb = mat->GetVB();
	}
	info.disableFullbright = mat->tex->gl_info.bDisableFullbright;
	info.transparent = mat->GetTransparent();
	return true;
}

MaterialRef MaterialForTextureIndex(int index)
{
	return FMaterial::ValidateTexture(TexMan.ByIndex(index), false);
}

MaterialRef MaterialForName(const char *name)
{
	const FTextureID id = TexMan.CheckForTexture(name, FTexture::TEX_Any);
	return id.isValid() ? FMaterial::ValidateTexture(id, false, true) : NULL;
}

MaterialRef MaterialCurrentFrame(MaterialRef m)
{
	if (m == NULL || Mat(m)->tex == NULL) return m;
	return MaterialShowing(Mat(m)->tex);
}

MaterialRef MaterialShowing(const void *texture)
{
	return texture ? FMaterial::ValidateTexture(((FTexture *)texture)->id, false, true) : NULL;
}

const char *MaterialName(MaterialRef m)
{
	return (m != NULL && Mat(m)->tex != NULL && Mat(m)->tex->Name.IsNotEmpty()) ? Mat(m)->tex->Name.GetChars() : "?";
}

MaterialRef MaterialBrightmap(MaterialRef m)
{
	if (m == NULL || Mat(m)->tex == NULL || Mat(m)->tex->gl_info.Brightmap == NULL) return NULL;
	return FMaterial::ValidateTexture(Mat(m)->tex->gl_info.Brightmap, false);
}

bool MaterialIsMasked(MaterialRef m) { return m != NULL && ((FMaterial *)m)->isMasked(); }

int MaterialTransparency(MaterialRef m)
{
	return (m != NULL && Mat(m)->tex != NULL) ? (int)Mat(m)->tex->gl_info.mIsTransparent : -1;
}

int MaterialTranslation(int translation)
{
	// [rc4l] As FGLTexture::Bind does: negative is already a GLTranslationPalette index, positive a game translation to look up.
	return translation <= 0 ? -translation : GLTranslationPalette::GetInternalTranslation(translation);
}

unsigned char *MaterialPixels(MaterialRef m, int translation, int &width, int &height)
{
	width = height = 0;
	if (m == NULL) return NULL;
	FMaterial *mat = (FMaterial *)m;
	// [rc4l] createexpanded=false: the expanded border exists for GL's clamp behaviour on sprites and would shift the UVs the mesh baked.
	unsigned char *buf = mat->CreateTexBuffer(translation, width, height, true, false);
	if (buf == NULL || width <= 0 || height <= 0)
	{
		delete[] buf;
		return NULL;
	}
	// [rc4l] GL's ProcessData: a see-through texture's edge texels take an opaque neighbour's colour, which a wall GL draws unmasked shows and filtering blends in.
	if (mat->tex->bMasked && !mat->tex->bHasCanvas) FTexture::SmoothEdges(buf, width, height);
	return buf;
}

}} // namespace zx::materials
