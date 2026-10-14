// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's sheen over a line mirror (GLWall::RenderMirrorSurface, r_mirror_envmap): glstuff/mirror.png sphere-mapped and added at a tenth of the wall's light.

#ifndef ZX_MIRRORSHEEN_COMPUTE_H
#define ZX_MIRRORSHEEN_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The alpha RenderMirrorSurface's gl_SetColor gives the sheen, which its SRC_ALPHA/ONE blend adds it by.
const float kMirrorSheenAlpha = 0.1f;

// [rc4l] Floats in the mirror pass's sheen block: the view, the mirror's eye-space normal, the light colour with the sheen's strength, and soft light, fog mode, fog density and r_mirror_recursions.
const int kMirrorSheenFloats = 28;

// [rc4l] GL's normal matrix (VSMatrix::computeNormalMatrix): the inverse transpose of a column-major matrix's 3x3, column-major, or false when it has no inverse.
bool ComputeNormalMatrix(const float m[16], float out[9]);

// [rc4l] main.vp's SPHEREMAP texture coordinate, for an eye-space position and an eye-space normal.
void ComputeSphereMapUV(const float eyePos[3], const float eyeNormal[3], float &s, float &t);

// [rc4l] The sheen block for one mirror whose mesh-space normal is (normX, 0, normZ), seen through view; strength 0 when GL drew no sheen on it, and how many mirrors deep GL draws reflections.
void ComputeMirrorSheenConstants(const float view[16], float normX, float normZ, const float rgb[3], float softLight,
	float fogMode, float fogDensity, bool drawn, int recursions, float out[kMirrorSheenFloats]);

}} // namespace zx::hwrender

#endif
