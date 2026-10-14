// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which of a sprite frame's eight rotations the viewer sees, as gl_GetSpriteFrame picks it.

#ifndef ZX_SPRITEFRAME_COMPUTE_H
#define ZX_SPRITEFRAME_COMPUTE_H

namespace zx { namespace sprites {

// [rc4l] The rotation for a view angle relative to the actor (BAM) and whether it is drawn mirrored, a symmetric frame (first two rotations sharing a texture) not being offset by half a step.
int ComputeSpriteRotation(unsigned int angle, bool symmetric, unsigned int flipMask, bool &mirror);

// [rc4l] Whether an actor is derived on the game thread rather than a lane: a model, which captures through the engine, and a player, whose damage tint keeps per-actor state.
bool ComputeSpriteOnGameThread(bool model, bool player);

}} // namespace zx::sprites

#endif // ZX_SPRITEFRAME_COMPUTE_H
