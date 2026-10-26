// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLDEFS' custom hardware shaders for a renderer other than GL: a mod's GL fragment code adapted to sit beside the backend's own shader, and a piece's shader as the backend numbers it.

#ifndef ZX_HWSHADER_COMPUTE_H
#define ZX_HWSHADER_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace hwshaders {

// [rc4l] GL's FIRST_USER_SHADER: a texture's shaderindex from here up names a GLDEFS shader.
const int kFirstUserShader = 12;

// [rc4l] The shader a piece draws with, in the slot the warp style takes: an ANIMDEFS warp (1 or 2) first, as GL refuses a hardware shader on a warped texture, else 3 plus its GLDEFS shader (userShader, -1 for none), else 0.
int ComputePieceShader(int warp, int userShader);

// [rc4l] A GLDEFS shader's GL source made to sit in the backend's: #version lines and the loose `uniform float timer;` dropped, gl_TexCoord[0] read as vTexCoord as GL's loader does, and ProcessTexel, Process and ProcessLight renamed with `index` so several can share one shader; true when it defines ProcessTexel, false for an old one that defines Process(color) instead.
bool ComputeAdaptUserShader(const std::string &source, int index, std::string &out);

// [rc4l] The block the backend's scene shaders take its user shaders from: getTexel, texture2D, vTexCoord and timer as GL's main.fp offers them, each adapted shader (newStyle saying which entry it has, an empty one left out so its pieces sample their texture), and fuaSurfaceTexel, which runs a piece's shader or else samples its texture.
std::string ComputeUserShaderBlock(const std::vector<std::string> &adapted, const std::vector<bool> &newStyle);

// [rc4l] source with the line holding `marker` replaced by block, or source as it is when it has no marker.
std::string ComputeSpliceShaderBlock(const std::string &source, const std::string &marker, const std::string &block);

// [Doruk-Aksoy] The console note a renderer prints for a GLDEFS shader it leaves out, with `why`'s non-blank lines indented under it.
std::string ComputeUserShaderSkipNote(const std::string &renderer, const std::string &lump, const std::string &why);

}} // namespace zx::hwshaders

#endif // ZX_HWSHADER_COMPUTE_H
