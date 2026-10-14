// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The capture side's way into the light model: a surface's light and GL colormap, as GL's walk hands them over.

#ifndef ZX_CAPTURESHADING_H
#define ZX_CAPTURESHADING_H

#include "features/levelmesh/staticmesh.h"
#include "features/sprites/computation/spritestyle_compute.h"   // [rc4l] SpriteDrawLook

// [rc4l] Global scope, since a forward declaration inside zx::levelmesh would name a different type from the engine's.
struct FColormap;

namespace zx { namespace levelmesh {

// [rc4l] Shade `out` as gl_SetColor and gl_SetFog would light this surface; `noFog` is the RENDERWALL_M2SNF case and `additive` fogs to black.
void CaptureShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out,
                    bool noFog = false, bool additive = false);

// [rc4l] Shade a sprite or model piece as GLSprite::Draw does under `look`: fogged to black when additive and not at all under a fog layer, a shadow's fixed grey for its light, the actor's ThingColor over either, and the look's alpha.
void CaptureSpriteShading(int lightlevel, int rellight, const FColormap &cm, const zx::sprites::SpriteDrawLook &look,
                          unsigned thingColor, MeshPiece &out);

// [rc4l] Turn a copy of a fog-layer sprite's piece into GL's CM_FOGLAYER pass over it: fogged as gl_SetFog fogs it, drawn alpha-blended.
void CaptureFogLayer(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out);

// [rc4l] A level surface's shading, as CaptureShading but with no fixed colormap: the cached mesh keeps its light through a power, and the shader applies the colormap over it, so a power starting, blinking or ending never re-bakes the level.
void CaptureLevelShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog = false);

// [rc4l] gl_SetColor's colour alone, as FillSimplePoly sets it for a textured automap floor: software lighting's curve swapped for the standard one.
void CapturePolyColor(int lightlevel, const FColormap &cm, float &r, float &g, float &b);

// [rc4l] Another renderer's shading of the same surface, which fua_shading_check compares the light model against; features/sidebyside sets GL's while the check is on.
typedef void (*ShadingOracle)(int lightlevel, int rellight, const FColormap &cm, bool noFog, bool additive, MeshPiece &out);
void SetShadingOracle(ShadingOracle oracle);

}} // namespace zx::levelmesh

#endif // ZX_CAPTURESHADING_H
