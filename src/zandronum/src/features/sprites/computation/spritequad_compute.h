// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A sprite's quad as GLSprite builds it: its edges from the frame's offsets, and the four corners a billboard, an XY billboard or a rolled sprite ends up with.

#ifndef ZX_SPRITEQUAD_COMPUTE_H
#define ZX_SPRITEQUAD_COMPUTE_H

namespace zx { namespace sprites {

// [rc4l] The frame's rectangle after scaling: left and top are the offsets from the actor's origin.
struct SpriteRect { float left, top, width, height; };

// [rc4l] Where the quad lies: x1,y1 to x2,y2 along the ground, z1 its top and z2 its bottom.
struct SpriteEdges { float x1, y1, x2, y2, z1, z2; };

// [rc4l] The frame's rectangle mirrored about the origin when the rotation is drawn flipped, then scaled; uLeft and uRight are the u coordinates GL draws its two sides with.
SpriteRect ComputeSpriteRect(const SpriteRect &frame, float ul, float ur, bool mirror, float scaleX, float scaleY,
	float &uLeft, float &uRight);

// [rc4l] Edges for a sprite facing along (dirX, dirY): the view vector for a face sprite, the actor's angle for a wall sprite.
SpriteEdges ComputeSpriteEdges(float x, float y, float z, const SpriteRect &r, bool wallSprite, float dirX, float dirY);

// [rc4l] The four corners GLSprite::Draw hands GL, as (x, height, y): top left, top right, bottom left, bottom right.
// [rc4l] An XY billboard tilts with the camera's pitch and a rolled sprite turns about the sight line, both about the quad's centre.
void ComputeSpriteCorners(const SpriteEdges &e, bool xyBillboard, bool roll, float viewYaw, float viewPitch,
	float rollDegrees, float out[4][3]);

}} // namespace zx::sprites

#endif // ZX_SPRITEQUAD_COMPUTE_H
