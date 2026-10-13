// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwshaders/hwshaders.h"   // [rc4l] UserShaderOf
#include "features/materials/materials.h"
#include "w_wad.h"   // [rc4l] the special textures' lumps

#include "textures/textures.h"
#include "features/materials/material.h"
#include "features/materials/skyboxtexture.h"   // [rc4l] FSkyBox
#include "features/materials/textureinfo.h"   // [rc4l] gl_GenerateGlobalBrightmapFromColormap
#include "features/materials/translationpalette.h"   // [rc4l] FTranslationPalette
#include "r_data/r_translate.h"         // [rc4l] FRemapTable

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
	MaterialRenderSize(m, info.renderWidth, info.renderHeight);
	info.materialWidth = Mat(m)->GetWidth();
	info.leftOffset = Mat(m)->GetLeftOffset();
	info.topOffset = Mat(m)->GetTopOffset();
	info.masked = tex->bMasked;
	info.canvas = tex->bHasCanvas;
	info.warp = tex->bWarped;
	info.complex = tex->bComplex;
	info.basePalette = tex->UseBasePalette();
	info.brightmap = tex->gl_info.Brightmap != NULL;
	info.shaderSpeed = tex->gl_info.shaderspeed;
	info.userShader = zx::hwshaders::UserShaderOf(tex->gl_info.shaderindex);
	return true;
}

MaterialRef MaterialForTextureId(FTextureID id, bool translate)
{
	return FMaterial::ValidateTexture(id, false, translate);
}

FTexture *MaterialTexture(MaterialRef m)
{
	return m != NULL ? Mat(m)->tex : NULL;
}

bool MaterialIsCanvas(MaterialRef m)
{
	return m != NULL && Mat(m)->tex->bHasCanvas;
}

bool MaterialIsFullbright(MaterialRef m)
{
	return m != NULL && Mat(m)->tex->isFullbright();
}

void MaterialRenderSize(MaterialRef m, int &width, int &height)
{
	width = Mat(m)->TextureWidth();
	height = Mat(m)->TextureHeight();
}

static bool g_anySize = false;
static int g_maxTextureSize = 0;

void SetTextureLimits(bool anySize, int maxSize)
{
	g_anySize = anySize;
	g_maxTextureSize = maxSize;
}

void MaterialStoredSize(MaterialRef m, int &width, int &height)
{
	width = ComputeStoredDimension(Mat(m)->GetWidth(), g_maxTextureSize);
	height = ComputeStoredDimension(Mat(m)->GetHeight(), g_maxTextureSize);
}

void MaterialTexCoordInfo(MaterialRef m, FTexCoordInfo *tci, fixed_t xscale, fixed_t yscale)
{
	Mat(m)->GetTexCoordInfo(tci, xscale, yscale);
}

void AssumeBackendTextures(int maxTextureSize)
{
	SetTextureLimits(true, maxTextureSize > 0 ? maxTextureSize : g_maxTextureSize);
}

void GenerateGlobalBrightmap()
{
	gl_GenerateGlobalBrightmapFromColormap();
}

FNativePalette *CreateTranslationPalette(FRemapTable *remap)
{
	return FTranslationPalette::CreatePalette(remap);
}

bool SkyBoxFacesOf(FTexture *tex, SkyBoxFaces &out)
{
	if (tex == NULL || !tex->gl_info.bSkybox) return false;
	const FSkyBox *sb = static_cast<const FSkyBox *>(tex);
	for (int i = 0; i < 6; i++) out.faces[i] = sb->faces[i];
	out.flipTop = sb->fliptop;
	return true;
}

static unsigned g_texturesFlushed = 0;
void NoteTexturesFlushed() { g_texturesFlushed++; }
unsigned TexturesFlushed() { return g_texturesFlushed; }

bool MaterialTransparent(MaterialRef m)
{
	return m != NULL && Mat(m)->GetTransparent();
}

// [rc4l] glstuff's particle and mirror pictures, which FGLRenderer::Initialize created for itself before.
static FTexture *g_special[3] = { NULL, NULL, NULL };
static bool g_specialLoaded = false;

static void LoadSpecialTextures()
{
	if (g_specialLoaded) return;
	g_specialLoaded = true;
	static const char *names[3] = { "glstuff/glpart2.png", "glstuff/glpart.png", "glstuff/mirror.png" };
	for (int i = 0; i < 3; i++) g_special[i] = FTexture::CreateTexture(Wads.GetNumForFullName(names[i]), FTexture::TEX_MiscPatch);
}

FTexture *ParticleTexture(int style)
{
	LoadSpecialTextures();
	return (style == 1 || style == 2) ? g_special[style - 1] : NULL;
}

FTexture *MirrorSheenTexture()
{
	LoadSpecialTextures();
	return g_special[2];
}

MaterialRef MaterialForTexture(const void *texture, bool expand)
{
	return texture ? FMaterial::ValidateTexture((FTexture *)texture, expand) : NULL;
}

bool MaterialIfMade(const void *texture, bool expand, MaterialRef &out)
{
	FTexture *tex = (FTexture *)texture;
	out = NULL;
	// [rc4l] ValidateTexture's own tests, short of making the material it finds missing.
	if (tex == NULL || tex->UseType == FTexture::TEX_Null) return true;
	if (tex->gl_info.bNoExpand) expand = false;
	out = tex->gl_info.Material[expand];
	return out != NULL;
}

bool MaterialSpriteFrame(MaterialRef m, SpriteFrameInfo &info)
{
	if (m == NULL || Mat(m)->tex == NULL) return false;
	FMaterial *mat = (FMaterial *)m;
	FloatRect r;
	mat->GetSpriteRect(&r);
	info.left = r.left; info.top = r.top; info.width = r.width; info.height = r.height;
	// [rc4l] Trimming needs non-power-of-two textures, as GLSprite::Process checks.
	if (g_anySize)
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
	// [rc4l] As GL's BindPixels does: negative is already a FTranslationPalette index, positive a game translation to look up.
	return translation <= 0 ? -translation : FTranslationPalette::GetInternalTranslation(translation);
}

int MaterialRemapIndex(const void *remap)
{
	const FRemapTable *table = (const FRemapTable *)remap;
	if (table == NULL || table->Inactive) return 0;
	FTranslationPalette *pal = static_cast<FTranslationPalette *>(const_cast<FRemapTable *>(table)->GetNative());
	return pal != NULL ? pal->GetIndex() : 0;
}

bool MaterialCorners(MaterialRef m, float &ul, float &vt, float &ur, float &vb)
{
	if (m == NULL) return false;
	const FMaterial *mat = (const FMaterial *)m;
	ul = mat->GetUL(); vt = mat->GetVT(); ur = mat->GetUR(); vb = mat->GetVB();
	return true;
}

unsigned char *MaterialPixels(MaterialRef m, int translation, int &width, int &height)
{
	width = height = 0;
	if (m == NULL) return NULL;
	FMaterial *mat = (FMaterial *)m;
	// [rc4l] A sprite's material is GL's framed one, a transparent texel round the picture that its sprite UVs and size count in, so its pixels come framed too; any other material has no frame to add.
	unsigned char *buf = mat->CreateTexBuffer(translation, width, height, true, true);
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
