// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A sprite as a traced mirror's ray finds it: a box the ray query offers, and the quad a mirror's walk turned to face that reflection's eye, as GL draws it there.

#ifndef ZX_TRACEDSPRITE_COMPUTE_H
#define ZX_TRACEDSPRITE_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] In mesh space (x, height, z): its first corner and the edges to the second and third, its texture's u along the first edge and v along the second, the record it is shaded from, and the mirror view whose walk took it.
struct TracedSprite
{
	float origin[3], edgeU[3], edgeV[3];
	float u0, u1, v0, v1;
	unsigned piece;
	int view;
};

// [rc4l] The floats a sprite takes in the buffer the mirror shader reads, four vec4s.
enum { kTracedSpriteFloats = 16 };

// [rc4l] Whether a dynamic run is a sprite a traced ray meets: one quad, no model, decal or HUD part, taken by a line mirror's walk (mirrorView from 0) outside any stacked view, and opaque or alpha-tested, since a ray stops at what it meets and blends nothing.
bool ComputeTracesRun(unsigned vertexCount, bool solid, bool depthBias, bool hud, int stackSlot, int onMirror, int mirrorView, int blend);

// [rc4l] A sprite from its quad's first three corners (x, height, z, u, v each), the second across from the first and the third down from it; false for one with no area.
bool ComputeTracedSprite(const float *first, const float *second, const float *third, unsigned piece, int view, TracedSprite &out);

// [rc4l] The box around its four corners, min then max corner.
void ComputeTracedSpriteBox(const TracedSprite &s, float mn[3], float mx[3]);

// [rc4l] Its floats for the shader: (origin, piece), (edgeU, view), (edgeV, 0), (u0, u1, v0, v1).
void ComputeTracedSpriteRecord(const TracedSprite &s, float out[kTracedSpriteFloats]);

// [rc4l] The floats ahead of the sprites in that buffer: a vec4 holding how many steps follow, then a vec4 a step.
unsigned ComputeTracedSpriteHeaderFloats(unsigned stepCount);

// [rc4l] That header, from ComputeMirrorSteps' triples: (count, 0, 0, 0), then (from view, mirror line, to view, 0) a step.
void ComputeTracedSpriteHeader(const int *steps, unsigned stepCount, float *out);

// [rc4l] Where the ray from o along d meets the quad from either side, between tMin and tMax, and the texture coordinate there; false for a miss.
bool ComputeTracedSpriteHit(const TracedSprite &s, const float o[3], const float d[3], float tMin, float tMax, float &t, float &u, float &v);

}} // namespace zx::hwrender

#endif // ZX_TRACEDSPRITE_COMPUTE_H
