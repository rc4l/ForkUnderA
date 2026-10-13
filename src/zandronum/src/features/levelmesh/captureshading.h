// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The capture side's way into the light model: a surface's light and GL colormap, as GL's walk hands them over.

#ifndef ZX_CAPTURESHADING_H
#define ZX_CAPTURESHADING_H

#include "features/levelmesh/staticmesh.h"

// [rc4l] Global scope, since a forward declaration inside zx::levelmesh would name a different type from the engine's.
struct FColormap;

namespace zx { namespace levelmesh {

// [rc4l] Shade `out` as gl_SetColor and gl_SetFog would light this surface; `noFog` is the RENDERWALL_M2SNF case and `additive` fogs to black.
void CaptureShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out,
                    bool noFog = false, bool additive = false);

// [rc4l] gl_SetColor's colour alone, as FillSimplePoly sets it for a textured automap floor: software lighting's curve swapped for the standard one.
void CapturePolyColor(int lightlevel, const FColormap &cm, float &r, float &g, float &b);

}} // namespace zx::levelmesh

#endif // ZX_CAPTURESHADING_H
