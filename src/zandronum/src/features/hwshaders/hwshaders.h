// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLDEFS' HardwareShader definitions for every renderer: which lumps a mod's textures name, kept here rather than in GL's shader manager so the backend can compile them too.

#ifndef ZX_HWSHADERS_H
#define ZX_HWSHADERS_H

#include "features/hwshaders/computation/hwshader_compute.h"

class FScanner;

namespace zx { namespace hwshaders {

// [rc4l] One GLDEFS HardwareShader block, as gl_ParseHardwareShader read it: the texture's shader index and speed set, its lump listed once.
void ParseHardwareShader(FScanner &sc, int deflump);

// [rc4l] How many shaders the mod's textures name, one per lump and lighting, and the full lump name of shader `i`, whose textures carry shaderindex kFirstUserShader + i.
int UserShaderCount();
const char *UserShaderLump(int i);

// [Doruk-Aksoy] How shader `i`, an old Process(color) one, is lit (its block's fua_ProcessLighting).
ProcessLighting UserShaderLighting(int i);

// [rc4l] The GLDEFS shader a texture's shaderindex names, or -1.
int UserShaderOf(int shaderIndex);

}} // namespace zx::hwshaders

#endif // ZX_HWSHADERS_H
