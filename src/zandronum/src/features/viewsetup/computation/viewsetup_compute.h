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

}} // namespace zx::viewsetup

#endif // ZX_VIEWSETUP_COMPUTE_H
