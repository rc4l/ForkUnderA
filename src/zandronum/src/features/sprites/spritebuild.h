// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Sprites from game state: an actor's billboard derived here, as GLSprite::Process and Draw derive it, and handed to the backend's dynamic stream.

#ifndef ZX_SPRITEBUILD_H
#define ZX_SPRITEBUILD_H

#include "basictypes.h"          // [rc4l] fixed_t, which renderstyle.h uses without including
#include "r_data/renderstyle.h"   // [rc4l] FRenderStyle
#include "features/sprites/computation/spritestyle_compute.h"

class AActor;
struct particle_t;
struct sector_t;
struct subsector_t;

namespace zx { namespace levelmesh { struct MeshPiece; struct MeshVertex; } }

namespace zx { namespace sprites {

// [rc4l] One drawn piece of a sprite: the quad's four corners and the record the backend shades it from.
struct SpritePiece;

// [rc4l] Derive an actor's sprite or model into the dynamic stream, noted for fua_sprite_check as well while it is on.
void BuildSprite(AActor *thing);

// [rc4l] BuildSprite for `count` actors, in that order: sprites derived on the job pool and appended in turn, models and a sprite whose material is not made yet on the game thread; not for the checks, which compare one actor at a time.
void BuildSprites(AActor *const *things, int count);

// [rc4l] Derive a particle in `sector` (as GL's walk sees it) into the dynamic stream, noted for the check likewise.
void BuildParticle(particle_t *particle, sector_t *sector);

// [rc4l] With fua_sprite_check on, what GL drew for an actor or a particle, its `count` vertices with it, so the derivation can be compared against it.
void NoteGLSprite(const void *who, bool particle, const zx::levelmesh::MeshPiece &piece, const zx::levelmesh::MeshVertex *verts, int count);

// [rc4l] With the check on, a derived model's surface, held for the comparison instead of drawn.
void NoteDerivedSprite(const void *who, const zx::levelmesh::MeshPiece &piece, const zx::levelmesh::MeshVertex *verts, int count);

// [rc4l] Compare every actor GL drew this frame against the derivation; called once GL's sprites are in.
void CheckFrameSprites();

// [rc4l] gl_SetDynSpriteLight for an actor: the dynamic light at its middle, into `out`, which starts from nothing.
void ActorDynamicLight(const AActor *actor, float out[3]);
// [rc4l] gl_SetDynSpriteLight at a point of a subsector with no actor, as a decal takes it, into `out`, which starts from nothing.
void PointDynamicLight(fixed_t x, fixed_t y, fixed_t z, subsector_t *subsec, float out[3]);

// [rc4l] An engine render style as spritestyle_compute takes it, and back.
SpriteStyle StyleOf(const FRenderStyle &s);
FRenderStyle RenderStyleOf(const SpriteStyle &s);
// [rc4l] The blend (MeshPiece::blendMode) an engine render style draws with at `alpha`.
int StyleBlendMode(const FRenderStyle &s, float alpha);

// [rc4l] Whether sprites, the weapon and decals take dynamic light this frame: lights on and in the view, no fixed colormap, and r_light_sprites.
bool SpritesTakeDynamicLight();

}} // namespace zx::sprites

#endif // ZX_SPRITEBUILD_H
