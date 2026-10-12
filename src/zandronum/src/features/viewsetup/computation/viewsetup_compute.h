// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The view's numbers as FGLRenderer::RenderViewpoint and FrustumAngle work them out: the pitch stretched by the level's pixel ratio, and the half-angle the walks are seeded with.

#ifndef ZX_VIEWSETUP_COMPUTE_H
#define ZX_VIEWSETUP_COMPUTE_H

namespace zx { namespace viewsetup {

// [rc4l] viewpitch (BAM) as a pitch in degrees once the level's pixel stretch is applied, as the playsim's 1:1 pitch looks on stretched pixels.
float ComputeStretchedPitch(unsigned viewPitch, float pixelStretch);

// [rc4l] FrustumAngle: half the angle the view can see either side of its heading, widened for a tilted view, or 2^32-1 for one pitched past 46 degrees or past half a turn.
unsigned ComputeFrustumAngle(float pitchDeg, float fovDeg, int aspectMultiplier);

// [rc4l] How far a sky layer has scrolled at the frame's clock, in degrees, as RenderViewpoint keeps mSky1Pos and mSky2Pos.
float ComputeSkyScroll(long frameMS, float skySpeed);

// [rc4l] SetViewAngle: the yaw GL's matrices take for a view angle (BAM), and the unit vector it faces in the plane.
void ComputeViewAngleVector(unsigned viewAngle, float &yaw, float &vectorX, float &vectorY);

// [rc4l] GL's area_t, where the eye is against a Boom height transfer.
enum { kAreaNormal = 0, kAreaBelow = 1, kAreaAbove = 2, kAreaDefault = 3 };

// [rc4l] SetViewArea: below the transfer's floor, above its ceiling (unless it fakes only a floor), between, or no transfer at all (left to the walk to find).
int ComputeViewArea(bool transfer, long long viewZ, long long transferFloorZ, long long transferCeilingZ, bool fakeFloorOnly);

// [rc4l] SetViewArea keeps the eye four units inside its sector's ceiling and floor, the floor winning where they cross; raw 16.16.
long long ComputeClampedViewZ(long long viewZ, long long floorZ, long long ceilingZ);

}} // namespace zx::viewsetup

#endif // ZX_VIEWSETUP_COMPUTE_H
