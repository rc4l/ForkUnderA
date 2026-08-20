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
struct FFlatVertex;
struct FColormap;
struct sector_t;
namespace zx { namespace levelmesh { struct MeshRange; } }

namespace zx { namespace levelmesh {

// Bake one subsector's plane into the mesh and register it as a piece. Cheap and idempotent: a
// (subsector, plane) already baked at the same geometry is skipped.
void RegisterFlatSubsector(const GLFlat &flat, subsector_t *sub, bool ceiling);

// [rc4l] How often a visible flat was left alone rather than rebuilt. A cache whose hit rate
// nobody can see is a cache nobody can tell has stopped working.
void GetFlatCacheStats(int &hits, int &rebuilds);

// [rc4l] One captured flat, read-only: which subsector and side it is, which sector and plane it
// was taken from, and where its vertices live. For checking a derivation against the capture.
bool CachedFlat(int index, const subsector_t **sub, bool *ceiling, const sector_t **model,
	int *whichPlane, MeshRange *range);

// Forget every baked flat -- called when the wall cache is invalidated, so the two stay in step.
void ClearFlats();
// How many flat registrations were a subsector own plane vs a 3D floor plane.
void GetFlatStats(int &own, int &threeD);

int FlatPieceCount();

// [rc4l] Sprites. Unlike walls and flats these MOVE, so the range is keyed on the actor and reused
// in place -- registering a fresh range each frame would grow the buffer without bound, which is the
// failure that killed an early version of the wall cache.
void RegisterSprite(const GLSprite &spr);

// Sprite render styles seen since load, indexed by STYLEOP_*. See fua_spritestyles.
void GetSpriteStyleStats(int *ops16, int &flagsSeen);
void GetSpriteStyleDetail(int *dest16, int *classified4);

// [rc4l] One decal quad, captured where GLWall::DrawDecal emits its four vertices.
//
// Decals live on a sidedef's AttachedDecals list and are re-walked every frame, so they belong in
// the DYNAMIC stream with the sprites rather than the baked mesh: no invalidation to get wrong when
// one spawns, fades or is replaced. `quad` is four FFlatVertex in the fan order GL emits.
void RegisterDecal(const FFlatVertex *quad, const void *material, int translation,
                   bool shadow, bool additive, float alpha,
                   int lightlevel, int rel, const FColormap &colormap,
                   bool redToAlpha, unsigned int alphaColor,
                   float sortX, float sortY, float sortZ, const float *dynLight);

// [rc4l] The same, for a mark whose shape is not a quad: a projected decal, clipped to whatever
// geometry it landed on. `tris` is a triangle LIST, so count is a multiple of three.
void RegisterDecalTriangles(const FFlatVertex *tris, int count, const void *material, int translation,
                            bool shadow, bool additive, float alpha,
                            int lightlevel, int rel, const FColormap &colormap,
                            bool redToAlpha, unsigned int alphaColor,
                            float sortX, float sortY, float sortZ, const float *dynLight);
void ClearSprites();
int SpritePieceCount();

// [rc4l] Print this frame's sprite pieces -- z range, texture-coordinate range, light, resolved
// colour, alpha, blend. See fua_sprites.
void DumpSpriteNotes();

}} // namespace zx::levelmesh

#endif // ZX_FLATMESH_H
