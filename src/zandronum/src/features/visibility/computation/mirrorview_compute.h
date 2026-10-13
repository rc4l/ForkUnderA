// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A line mirror's view as GL's GLMirrorPortal makes it: the reflected eye, and the light its sheen is drawn in.

#ifndef ZX_MIRRORVIEW_COMPUTE_H
#define ZX_MIRRORVIEW_COMPUTE_H

namespace zx { namespace visibility {

// [rc4l] GLMirrorPortal::DrawContents's eye: (eyeX, eyeY) reflected across the line from v1 to v2, nudged off it as GL nudges it (half a unit for a straight line, half a unit per render depth along a slanted one's normal); all raw 16.16.
void ComputeMirrorEye(long long v1x, long long v1y, long long v2x, long long v2y, long long eyeX, long long eyeY, int depth,
	long long &outX, long long &outY);

// [rc4l] A light-list boundary as GLWall::SplitWall reads it: secplane_t's raw a, b, d and ic, and whether it slopes.
struct SheenPlane { double a, b, d, ic; bool sloped; };

// [rc4l] A wall's ends and heights as SplitWall takes them.
struct SheenWall { float x1, y1, x2, y2; float ztop[2], zbottom[2]; };

enum { kSheenOwnLight = -1, kSheenNothing = -2 };

// [rc4l] GLWall::SplitWall as far as its first piece, the one AddMirrorSheen takes: the light-list entry it is put with (Put3DWall), kSheenOwnLight for the wall's own (PutWall), or kSheenNothing; planes[i] is lightlist[i + 1]'s, of `bands` entries.
int ComputeSheenBand(const SheenWall &wall, const SheenPlane *planes, int bands);

// [rc4l] GLWall::Put3DWall's light for a sheen in a band: the band's own level unless it is the wall's sector's, and full under a fade wall.
int ComputeSheenBandLight(int wallLight, bool bandOwnsLevel, int bandLight, bool fadeWall);

}} // namespace zx::visibility

#endif // ZX_MIRRORVIEW_COMPUTE_H
