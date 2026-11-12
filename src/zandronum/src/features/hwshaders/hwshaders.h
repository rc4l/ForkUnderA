// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLDEFS' HardwareShader definitions for every renderer: which lumps a mod's textures name, kept here rather than in GL's shader manager so the backend can compile them too.

#ifndef ZX_HWSHADERS_H
#define ZX_HWSHADERS_H

#include "features/hwshaders/computation/hwshader_compute.h"

class FScanner;
class FTexture;

namespace zx { namespace hwshaders {

// [rc4l] One GLDEFS HardwareShader block, as gl_ParseHardwareShader read it: the texture's shader index and speed set, its lump listed once.
// [Doruk-Aksoy] ...with UZDoom's Texture, Define, DisableAlphaTest and Material lines, the Texture lines kept as the texture's layers.
void ParseHardwareShader(FScanner &sc, int deflump);

// [Doruk-Aksoy] uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad gldefs.cpp ParseMaterial: one GLDEFS Material block, its maps put on the texture and its shader typed by them.
void ParseMaterial(FScanner &sc, int deflump);

// [Doruk-Aksoy] One GLDEFS fua_Material block: its shader's key, whose uniforms each drawn actor's row holds (features/actormaterials).
void ParseActorMaterial(FScanner &sc);

// [Doruk-Aksoy] The GLDEFS shader a fua_Material draws its actors with, or -1.
int ActorMaterialShader(int material);

// [Doruk-Aksoy] The GLDEFS shader a texture draws with: its own when it fits the type its maps give, else that type's built-in one (ComputeMaterialShader), or -1.
int TextureUserShader(const FTexture *tex);

// [rc4l] How many shaders the mod's textures name, one per key (ComputeUserShaderSlot), and the full lump name of shader `i`, whose textures carry shaderindex kFirstUserShader + i.
int UserShaderCount();
const char *UserShaderLump(int i);

// [Doruk-Aksoy] Shader `i`'s whole key, or null past the last.
const UserShaderKey *UserShaderKeyOf(int i);

// [Doruk-Aksoy] How shader `i`, an old Process(color) one, is lit (its block's fua_ProcessLighting).
ProcessLighting UserShaderLighting(int i);

// [rc4l] The GLDEFS shader a texture's shaderindex names, or -1.
int UserShaderOf(int shaderIndex);

// [Doruk-Aksoy] The texture a texture's MaterialLayer `layer` reads, or null for none, and whether it has any.
FTexture *LayerTexture(const FTexture *tex, int layer);
bool HasLayers(const FTexture *tex);

// [Doruk-Aksoy] A texture's GLDEFS glossiness and specular level, packed as a layer row's spare entry (0 for UZDoom's defaults).
unsigned SpecularWord(const FTexture *tex);

}} // namespace zx::hwshaders

#endif // ZX_HWSHADERS_H
