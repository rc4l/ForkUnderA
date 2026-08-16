// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Flats (floors and ceilings) into the level mesh, so a backend gets the whole world rather
// than just its walls.
//
// Walls came through the wall cache because they are *derived* every frame and worth caching. Flats
// are different: FFlatVertexBuffer already keeps them in a static buffer with stable per-sector
// ranges, so nothing here needs to cache generation. What is needed is the same hand-off walls get --
// triangle-list geometry plus the material and light to shade it -- keyed so a subsector's floor and
// ceiling are each registered exactly once.
//
// The uv convention (`fx/64`, `-fy/64`) and the plane evaluation mirror GLFlat::DrawSubsector
// exactly; if that function changes, this must follow or flats will drift from the GL render.

#ifndef ZX_FLATMESH_H
#define ZX_FLATMESH_H

struct subsector_t;
class GLFlat;
class GLSprite;

namespace zx { namespace levelmesh {

// Bake one subsector's plane into the mesh and register it as a piece. Cheap and idempotent: a
// (subsector, plane) already baked at the same geometry is skipped.
void RegisterFlatSubsector(const GLFlat &flat, subsector_t *sub, bool ceiling);

// Forget every baked flat -- called when the wall cache is invalidated, so the two stay in step.
void ClearFlats();

int FlatPieceCount();

// [rc4l] Sprites. Unlike walls and flats these MOVE, so the range is keyed on the actor and reused
// in place -- registering a fresh range each frame would grow the buffer without bound, which is the
// failure that killed an early version of the wall cache.
void RegisterSprite(const GLSprite &spr);
void ClearSprites();
int SpritePieceCount();

}} // namespace zx::levelmesh

#endif // ZX_FLATMESH_H
