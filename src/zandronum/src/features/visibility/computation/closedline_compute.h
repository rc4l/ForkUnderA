// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] gl_CheckClip: whether a two-sided line blocks the view, as a shut door, a lift at the top, or a wall of heights does.

#ifndef ZX_CLOSEDLINE_COMPUTE_H
#define ZX_CLOSEDLINE_COMPUTE_H

namespace zx { namespace visibility {

// [rc4l] Both sectors' plane heights at the line's two ends, and what GL asks of the textures and skies.
struct ClosedLineIn
{
	bool stacked;              // [rc4l] either sector has a portal plane, which never blocks
	long long fsFloor[2], fsCeiling[2], bsFloor[2], bsCeiling[2];
	bool topTexture, bottomTexture;   // [rc4l] the side has a texture there that is not TEX_Null
	bool bothCeilingsSky, bothFloorsSky;
};

bool ComputeLineCloses(const ClosedLineIn &in);

// [rc4l] Whether the heights alone could close the line, so a caller reads the textures and skies only when they might matter.
bool ComputeLineMayClose(const ClosedLineIn &in);

}} // namespace zx::visibility

#endif // ZX_CLOSEDLINE_COMPUTE_H
