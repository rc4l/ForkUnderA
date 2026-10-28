// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLDEFS' custom hardware shaders for a renderer other than GL: a mod's GL fragment code adapted to sit beside the backend's own shader, and a piece's shader as the backend numbers it.

#ifndef ZX_HWSHADER_COMPUTE_H
#define ZX_HWSHADER_COMPUTE_H

#include <string>

namespace zx { namespace hwshaders {

// [rc4l] GL's FIRST_USER_SHADER: a texture's shaderindex from here up names a GLDEFS shader.
const int kFirstUserShader = 12;

// [rc4l] The shader a piece draws with, in the slot the warp style takes: an ANIMDEFS warp (1 or 2) first, as GL refuses a hardware shader on a warped texture, else 3 plus its GLDEFS shader (userShader, -1 for none), else 0.
int ComputePieceShader(int warp, int userShader);

// [Doruk-Aksoy] The GLDEFS shader a piece's shader slot (ComputePieceShader) names, or -1 for a warp or none.
int ComputeUserShaderOf(int pieceShader);

// [Doruk-Aksoy] The shader GL's 2D draw asks SetMaterial for, given the material's: -1 (the material's own) for a GLDEFS shader, which Zandronum 3.3 ran on a HUD image too, else 0, the default.
int ComputeDrawTextureShader(int materialShader);

// [Doruk-Aksoy] source with every whole-word `name[expr]`, its brackets matched, written as `func(expr)`.
std::string ComputeRewriteIndexedName(const std::string &source, const std::string &name, const std::string &func);

// [Doruk-Aksoy] Zandronum 3.3's names made to work on either renderer: loose declarations of camerapos and pixelpos dropped, camerapos read as uCameraPos.xyz unless the shader defines it, and gl_TextureMatrix[i] read through fuaTextureMatrix (7 the model matrix, 0 the texture matrix, else identity).
std::string ComputeLegacyShaderNames(const std::string &source);

// [rc4l] A GLDEFS shader's GL source made to sit in the backend's: #version lines and the loose `uniform float timer;` dropped, legacy names adapted (ComputeLegacyShaderNames), gl_TexCoord[0] read as vTexCoord as GL's loader does, and ProcessTexel, Process and ProcessLight renamed with `index`; true when it defines ProcessTexel, false for an old one that defines Process(color) instead.
bool ComputeAdaptUserShader(const std::string &source, int index, std::string &out);

// [Doruk-Aksoy] The backend shaders a GLDEFS shader's program is made from: the world's scene stages, whose entry is fuaSurfaceTexel (with fuaSurfaceAlpha), and the 2D layer's, whose entry is fua2DColor.
enum ProgramKind { kSceneProgram, k2DProgram };

// [Doruk-Aksoy] The line a kind's backend shader marks its program block's place with.
const char *ComputeProgramMarker(ProgramKind kind);

// [Doruk-Aksoy] The program block of one GLDEFS shader, `adapted` by ComputeAdaptUserShader with `index`: GL main.fp's names for what the kind's shader holds under its own (pixelpos, uCameraPos, vColor, ModelMatrix and the rest), the shader, and the kind's entry running it.
std::string ComputeUserProgramBlock(ProgramKind kind, const std::string &adapted, bool newStyle, int index);

// [Doruk-Aksoy] The program block of a kind's shader without one: its entry samples the texture.
std::string ComputePlainProgramBlock(ProgramKind kind);

// [rc4l] source with the line holding `marker` replaced by block, or source as it is when it has no marker.
std::string ComputeSpliceShaderBlock(const std::string &source, const std::string &marker, const std::string &block);

// [Doruk-Aksoy] The console line the backend prints after compiling the GLDEFS shaders at launch: how many shaders, into how many programs, in how long, and how many were left out.
std::string ComputePrecompileNote(int shaders, int programs, int leftOut, double ms);

// [Doruk-Aksoy] text's non-blank lines, each indented two spaces, as a console note lists its reasons.
std::string ComputeIndentedLines(const std::string &text);

// [Doruk-Aksoy] The console note a renderer prints for a GLDEFS shader it leaves out, with `why`'s non-blank lines indented under it.
std::string ComputeUserShaderSkipNote(const std::string &renderer, const std::string &lump, const std::string &why);

}} // namespace zx::hwshaders

#endif // ZX_HWSHADER_COMPUTE_H
