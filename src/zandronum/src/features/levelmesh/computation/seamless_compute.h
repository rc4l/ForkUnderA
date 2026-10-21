// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] r_seamless, as GL draws it (gl_RecalcVertexHeights, GLWall::SplitLeftEdge and the rest): the vertices a wall's fan gains where other surfaces meet its edges, so no T-junction leaves a crack.

#ifndef ZX_SEAMLESS_COMPUTE_H
#define ZX_SEAMLESS_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] The planes meeting at a vertex as gl_RecalcVertexHeights keeps them: `heights` (each sector's ceiling then floor there) ascending without repeats into out, none when two or fewer remain; how many.
int ComputeVertexHeights(const float *heights, int count, float *out);

// [rc4l] A fan vertex: x and y on the map, z its height, and its texture coordinate.
struct SeamVertex { float x, y, z, u, v; };

// [rc4l] What a wall's fan splits at, and which of its edges may split.
struct SeamSplits
{
	const float *leftHeights;  int leftCount;    // [rc4l] the left end vertex's ComputeVertexHeights
	const float *rightHeights; int rightCount;   // [rc4l] the right end vertex's
	const float *segX, *segY, *segFrac; int segCount;   // [rc4l] where each of the sidedef's segs but the last ends, and how far along the sidedef, ascending
	float fracLeft, fracRight;                   // [rc4l] the part of the sidedef the wall covers
	bool left, right, upper, lower;              // [rc4l] which edges split: r_seamless on, the end reached, and no GLWF_NOSPLITUPPER or LOWER
};

// [rc4l] Whether GL draws a wall through RenderWall's split: not one blended in its translucent pass (RWF_NOSPLIT) and not a middle between one sector on both sides (GLWF_NOSPLIT).
bool ComputeWallTakesSeams(bool blended, bool sameSectorMiddle);

// [rc4l] Which edges of band `band` of `bands` (top down) may split: SplitWall marks the piece above each light boundary GLWF_NOSPLITLOWER and the one below GLWF_NOSPLITUPPER.
void ComputeBandSeams(int band, int bands, bool &upper, bool &lower);

// [rc4l] The fan RenderWall draws from corners (lower left, upper left, upper right, lower right, its tcs 0-3) with s's vertices on its edges, into out; how many, or 0 when more than max.
int ComputeSeamlessFan(const SeamVertex corners[4], const SeamSplits &s, SeamVertex *out, int max);

}} // namespace zx::levelmesh

#endif // ZX_SEAMLESS_COMPUTE_H
