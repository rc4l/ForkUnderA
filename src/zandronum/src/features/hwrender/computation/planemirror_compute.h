// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Reflective floors and ceilings (Sector_SetPlaneReflection) as GL's GLPlaneMirrorPortal draws them: the world seen from the eye mirrored through the plane, clipped to the plane's own side.

#ifndef ZX_PLANEMIRROR_COMPUTE_H
#define ZX_PLANEMIRROR_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] One mirror plane: a floor or a ceiling, at a height.
struct MirrorPlane
{
	bool  ceiling;
	float z;
	bool operator==(const MirrorPlane &o) const { return ceiling == o.ceiling && z == o.z; }
};

// [rc4l] Whether the eye is on the reflecting side: at or above a floor, at or below a ceiling, as GLWall::SkyPlane decides.
bool ComputePlaneMirrorFaces(bool ceiling, float planeZ, float viewZ);

// [rc4l] The mirrored eye's height: as far behind the plane as the eye is in front of it.
float ComputePlaneMirrorEyeZ(float planeZ, float viewZ);

// [rc4l] The clip plane, in mesh space (x, z-up, y), that keeps only what the mirror faces; its w is never 0, which the shader reads as "no clip".
void ComputePlaneMirrorClip(bool ceiling, float planeZ, float out[4]);

// [rc4l] The reflection slot for a plane: one already taken by the same plane, a new one while fewer than maxSlots are used, else -1.
int ComputePlaneMirrorSlot(MirrorPlane *slots, int &used, int maxSlots, MirrorPlane p);

// [rc4l] Whether any part of a box can be on screen through a column-major view-projection with Vulkan depth (0..w): false only when all eight corners are outside one clip plane.
bool ComputeBoxMayBeVisible(const float *mvp, const float mn[3], const float mx[3]);

}} // namespace zx::hwrender

#endif // ZX_PLANEMIRROR_COMPUTE_H
