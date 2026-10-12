// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Materials for a renderer that must not know GL's FMaterial: a texture's identity, its current frame, brightmap and pixels.

#ifndef ZX_MATERIALS_H
#define ZX_MATERIALS_H

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

// [rc4l] The material for an engine texture (an FTexture), or for an index into the texture manager.
MaterialRef MaterialForTexture(const void *texture);
MaterialRef MaterialForTextureIndex(int index);
// [rc4l] The material a texture name looks up to, through animation and switch translation.
MaterialRef MaterialForName(const char *name);

// [rc4l] The frame an animated material shows now, or the material itself when it does not animate.
MaterialRef MaterialCurrentFrame(MaterialRef m);
// [rc4l] The material an engine texture shows now, through its animation.
MaterialRef MaterialShowing(const void *texture);

// [rc4l] The texture's name, "?" when there is none, for reports.
const char *MaterialName(MaterialRef m);

// [rc4l] The material's brightmap, NULL when it has none.
MaterialRef MaterialBrightmap(MaterialRef m);

// [rc4l] Whether GL draws it alpha-tested, which a texture with a mask can still avoid.
bool MaterialIsMasked(MaterialRef m);

// [rc4l] The texture's own transparency as GL measured it, for a diagnostic.
int MaterialTransparency(MaterialRef m);

// [rc4l] A translation in either convention (a negated palette index, or an actor's game translation) as the palette index pixels are made under.
int MaterialTranslation(int translation);

// [rc4l] RGBA pixels under a palette index from MaterialTranslation, edge-smoothed where GL's upload smooths them; the caller deletes[] them, NULL when there are none.
unsigned char *MaterialPixels(MaterialRef m, int translation, int &width, int &height);

}} // namespace zx::materials

#endif // ZX_MATERIALS_H
