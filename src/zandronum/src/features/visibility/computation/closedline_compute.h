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

// [rc4l] A flat plane's height as GL takes it from the plane's d: a floor's is -d and a ceiling's d.
long long ComputeFlatPlaneHeight(bool floor, long long d);

// [rc4l] gl_CheckViewArea's test on a line from the open into a height transfer: the eye is below the water when the front ceiling sits at or under the transfer's floor at both ends (raw 16.16, with GL's 1/100 tolerance).
bool ComputeEyeBelowTransfer(long long ceiling1, long long ceiling2, long long transferFloor1, long long transferFloor2);

// [rc4l] Whether the heights alone could close the line, so a caller reads the textures and skies only when they might matter.
bool ComputeLineMayClose(const ClosedLineIn &in);

}} // namespace zx::visibility

#endif // ZX_CLOSEDLINE_COMPUTE_H
