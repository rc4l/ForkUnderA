// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which of a sprite frame's eight rotations the viewer sees, as gl_GetSpriteFrame picks it.

#ifndef ZX_SPRITEFRAME_COMPUTE_H
#define ZX_SPRITEFRAME_COMPUTE_H

namespace zx { namespace sprites {

// [rc4l] The rotation for a view angle relative to the actor (BAM), and whether that rotation is drawn mirrored.
// [rc4l] A frame whose first two rotations share a texture is a symmetric one and is not offset by half a step.
int ComputeSpriteRotation(unsigned int angle, bool symmetric, unsigned int flipMask, bool &mirror);

}} // namespace zx::sprites

#endif // ZX_SPRITEFRAME_COMPUTE_H
