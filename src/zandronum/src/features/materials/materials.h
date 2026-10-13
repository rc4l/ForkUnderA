// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Materials for a renderer that must not know GL's FMaterial: a texture's identity, its current frame, brightmap and pixels.

#ifndef ZX_MATERIALS_H
#define ZX_MATERIALS_H

class FTexture;
struct FTexCoordInfo;
class FNativePalette;
struct FRemapTable;
#include "textures/textures.h"   // [rc4l] FTextureID

namespace zx { namespace materials {

// [rc4l] A material's identity, stable for the level and compared by address; only this module looks inside it.
typedef const void *MaterialRef;

// [rc4l] What a renderer asks of a material besides its pixels.
struct MaterialInfo
{
	const char *name;
	int   textureIndex;   // [rc4l] its index in the texture manager, which ANIMDEFS frames are numbered by
	int   width, height;
	int   renderWidth, renderHeight;   // [rc4l] the size GL draws it at, after scaling and trimming
	int   materialWidth;               // [rc4l] its width in texels as a u is measured, border included
	int   leftOffset, topOffset;       // [rc4l] its offsets in render units, as a decal hangs it
	bool  masked;         // [rc4l] has see-through texels
	bool  canvas;         // [rc4l] a camera texture, drawn every frame rather than loaded
	int   warp;           // [rc4l] ANIMDEFS warp: 0 none, 1 warp, 2 warp2
	bool  complex;
	bool  basePalette;    // [rc4l] drawn in the base palette, which decals use as an alpha mask
	bool  brightmap;      // [rc4l] has a brightmap (GLDEFS, or the default one GZDoom makes)
	float shaderSpeed;    // [rc4l] GLDEFS speed for a warp
};

// [rc4l] Fill `info`; false when there is no material or it has no texture.
bool DescribeMaterial(MaterialRef m, MaterialInfo &info);

// [rc4l] The material for an engine texture (an FTexture), expanded with the border a face sprite is drawn with when asked, or for an index into the texture manager.
MaterialRef MaterialForTexture(const void *texture, bool expand = false);
// [rc4l] MaterialForTexture without making one, so a worker thread may ask: false when the material is not made yet, true with `out` the material or NULL for a texture that has none.
bool MaterialIfMade(const void *texture, bool expand, MaterialRef &out);

// [rc4l] The round (1) and smooth (2) particle pictures gl_particles_style picks, NULL for square ones; loaded once and kept.
FTexture *ParticleTexture(int style);

// [rc4l] The sheen a mirror is drawn with (gl_mirror_envmap); loaded once and kept.
FTexture *MirrorSheenTexture();
MaterialRef MaterialForTextureIndex(int index);
// [rc4l] The material a texture name looks up to, through animation and switch translation.
MaterialRef MaterialForName(const char *name);

// [rc4l] The frame an animated material shows now, or the material itself when it does not animate.
MaterialRef MaterialCurrentFrame(MaterialRef m);
// [rc4l] The material an engine texture shows now, through its animation.
MaterialRef MaterialShowing(const void *texture);

// [rc4l] The texture's name, "?" when there is none, for reports.
const char *MaterialName(MaterialRef m);

// [rc4l] Whether the material has partly see-through texels, which send a two-sided middle to GL's translucent pass; the first ask decodes the picture, so only the main thread asks.
bool MaterialTransparent(MaterialRef m);

// [rc4l] The material a texture id is drawn with on a wall or flat (no border, through animation and switches unless `translate` is off), NULL for none.
MaterialRef MaterialForTextureId(FTextureID id, bool translate = true);
// [rc4l] The engine texture a material draws, NULL for none.
FTexture *MaterialTexture(MaterialRef m);
// [rc4l] The size GL draws a material at, and the size of the texture it lives in, which a canvas rounds to the hardware's rule.
void MaterialRenderSize(MaterialRef m, int &width, int &height);
void MaterialStoredSize(MaterialRef m, int &width, int &height);
// [rc4l] How a wall's texture coordinates scale and offset on the material at a sidedef's scale, as FMaterial::GetTexCoordInfo works them out.
void MaterialTexCoordInfo(MaterialRef m, FTexCoordInfo *tci, fixed_t xscale, fixed_t yscale);

// [rc4l] Set up the texture system for a launch with no GL context: what gl_LoadExtensions would find on any card the backend runs on, and its largest texture.
void AssumeBackendTextures(int maxTextureSize);
// [rc4l] The brightmap colormap textures are made against, again after the palette loads.
void GenerateGlobalBrightmap();
// [rc4l] A translation's palette as the texture system keeps it.
FNativePalette *CreateTranslationPalette(FRemapTable *remap);

// [rc4l] A GLDEFS sky box's faces (front, right, back, left, top, bottom; with three, one wrapped wall then top and bottom) and whether its top is flipped.
struct SkyBoxFaces { FTexture *faces[6]; bool flipTop; };
// [rc4l] The sky box `tex` is, false when it is an ordinary texture.
bool SkyBoxFacesOf(FTexture *tex, SkyBoxFaces &out);

// [rc4l] The material's brightmap, NULL when it has none.
MaterialRef MaterialBrightmap(MaterialRef m);

// [rc4l] What a sprite frame is drawn with: its rectangle about the actor's origin and the texture coordinates of its trimmed edges.
struct SpriteFrameInfo
{
	float left, top, width, height;
	float ul, ur, vt, vb;
	bool  disableFullbright;   // [rc4l] GLDEFS turns the actor's fullbright off for this frame
	bool  transparent;         // [rc4l] it has translucent texels, so it is blended rather than alpha-tested
};
bool MaterialSpriteFrame(MaterialRef m, SpriteFrameInfo &info);

// [rc4l] Whether GL draws it alpha-tested, which a texture with a mask can still avoid.
bool MaterialIsMasked(MaterialRef m);

// [rc4l] The texture's own transparency as GL measured it, for a diagnostic.
int MaterialTransparency(MaterialRef m);

// [rc4l] A translation in either convention (a negated palette index, or an actor's game translation) as the palette index pixels are made under.
int MaterialTranslation(int translation);

// [rc4l] An engine remap table (FRemapTable) as the palette index pixels are made under, 0 for none or an inactive one.
int MaterialRemapIndex(const void *remap);

// [rc4l] The texture coordinates of the material's corners, as a 2D draw spans them; false, and nothing written, without a material.
bool MaterialCorners(MaterialRef m, float &ul, float &vt, float &ur, float &vb);

// [rc4l] RGBA pixels under a palette index from MaterialTranslation, edge-smoothed where GL's upload smooths them; the caller deletes[] them, NULL when there are none.
unsigned char *MaterialPixels(MaterialRef m, int translation, int &width, int &height);

}} // namespace zx::materials

#endif // ZX_MATERIALS_H
