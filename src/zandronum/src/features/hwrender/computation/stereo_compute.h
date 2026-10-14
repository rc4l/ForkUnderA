// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's stereo 3D (gl/stereo3d) for the backend: which eyes vr_mode draws, where each stands and how its frustum leans, and the colour mask an anaglyph blends it through.

#ifndef ZX_STEREO_COMPUTE_H
#define ZX_STEREO_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The eyes a vr_mode draws: how many, each one's shift in interpupillary distances (-0.5 left, +0.5 right, 0 the middle) and the channels it keeps (1 kept, 0 masked).
struct StereoMode
{
	int eyes;
	float shift[2];
	float mask[2][3];
};

// [rc4l] Stereo3DMode::getCurrentMode by vr_mode: 1 green/magenta and 2 red/cyan anaglyphs, 5 the left eye alone and 6 the right, anything else one middle eye.
StereoMode ComputeStereoMode(int vrMode);

// [rc4l] ShiftedEyePose::GetViewShift through ScopedViewShifter: how far the eye moves on the map for a shift of shiftMeters at yaw yawDeg (GL's mAngles.Yaw), at hunitsPerMeter map units a meter.
void ComputeEyeOffset(float shiftMeters, float yawDeg, float hunitsPerMeter, float &dx, float &dy);

// [rc4l] ShiftedEyePose::GetProjection's asymmetric frustum as a shift of NDC x: the eye's shift over the screen distance, against the frustum's half width at a vertical field of view fovY (radians) and aspect.
float ComputeEyeNdcShift(float shiftMeters, float screenDist, float fovY, float aspect);

// [rc4l] m with ndc added to every point's NDC x, as the leaning frustum moves it.
void ComputeShiftNdcX(float ndc, float m[16]);

}} // namespace zx::hwrender

#endif // ZX_STEREO_COMPUTE_H
