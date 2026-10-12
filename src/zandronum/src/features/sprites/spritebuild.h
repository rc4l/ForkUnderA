// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Sprites from game state: an actor's billboard derived here, as GLSprite::Process and Draw derive it, and handed to the backend's dynamic stream.

#ifndef ZX_SPRITEBUILD_H
#define ZX_SPRITEBUILD_H

class AActor;

namespace zx { namespace levelmesh { struct MeshPiece; struct MeshVertex; } }

namespace zx { namespace sprites {

// [rc4l] One drawn piece of a sprite: the quad's four corners and the record the backend shades it from.
struct SpritePiece;

// [rc4l] Derive an actor's sprite into the dynamic stream; false when GL must draw it, which a model needs.
bool BuildSprite(AActor *thing);

// [rc4l] With fua_sprite_check on, what GL's RegisterSprite recorded for an actor, so the derivation can be compared against it.
void NoteGLSprite(const AActor *actor, const zx::levelmesh::MeshPiece &piece, const zx::levelmesh::MeshVertex *quad);

// [rc4l] Compare every actor GL drew this frame against the derivation; called once GL's sprites are in.
void CheckFrameSprites();

}} // namespace zx::sprites

#endif // ZX_SPRITEBUILD_H
