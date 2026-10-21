// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Line_Horizon as engine-free numbers (map space, z up): which lines, what each half paints, and where a ray meets the plane.

#ifndef ZX_HORIZON_COMPUTE_H
#define ZX_HORIZON_COMPUTE_H

namespace zx { namespace horizon {

// [rc4l] Line_Horizon's special number, from actionspecials.h.
constexpr int kLineHorizonSpecial = 9;

// [rc4l] How far GL's sky surfaces reach above a sky ceiling and below a sky floor (SkyNormal).
constexpr float kHorizonSkyReach = 32768.f;

// [rc4l] Half the side of the square GLHorizonPortal draws its plane over, centred on the eye.
constexpr float kHorizonHalfExtent = 32768.f;

// [rc4l] Whether a line is a horizon, which draws no wall parts on either side.
bool ComputeIsHorizonLine(int lineSpecial);

// [rc4l] Which halves are planes to ray-cast and which is sky; a sky floor is left undrawn, like every other sky floor.
struct HorizonHalves
{
	bool floorPlane;
	bool ceilingPlane;
	bool skyAbove;
};
HorizonHalves ComputeHorizonHalves(bool floorSky, bool ceilingSky);

// [rc4l] A vertical extent on the line.
struct HorizonSpan
{
	float bottom, top;
};

// [rc4l] How far the window reaches: the sector's floor to ceiling, stretched to the sky's reach on a sky side.
HorizonSpan ComputeHorizonWindowSpan(float floorZ, float ceilingZ, bool floorSky, bool ceilingSky);

// [rc4l] DoHorizon's split between the halves: the eye's height, clamped to the sector's span.
float ComputeHorizonSplit(float eyeZ, float floorZ, float ceilingZ);

// [rc4l] The slice one half paints for an eye at eyeZ -- floor..split or split..ceiling, empty when the eye leaves none.
HorizonSpan ComputeHorizonHalfSpan(bool ceilingHalf, float floorZ, float ceilingZ, float eyeZ);

// [rc4l] Where a window's sky quad starts for an eye at eyeZ: the split when a floor plane paints below it, as DoHorizon starts SkyPlane at viewz, else the window's own bottom.
float ComputeHorizonSkyBottom(bool floorPlane, float windowBottom, float floorZ, float ceilingZ, float eyeZ);

// [rc4l] The light GLHorizonPortal shades a plane with: full and unrelative for a fullbright flat, else the sector's.
void ComputeHorizonLightInputs(bool fullbright, int lightLevel, int extraLight, int &level, int &rel);

// [rc4l] Where a ray from the eye through a window point meets the plane, stopped at GL's square edge where its gap strips stand.
struct HorizonHit
{
	float x, y, z;
	bool  withinSquare;
};
HorizonHit ComputeHorizonRayHit(float eyeX, float eyeY, float eyeZ,
                                float throughX, float throughY, float throughZ,
                                float planeZ, float halfExtent);

// [rc4l] Whether the eye is on the side the window faces, the right of (x1,y1)->(x2,y2) where its sector is.
bool ComputeHorizonFacesEye(float x1, float y1, float x2, float y2, float eyeX, float eyeY);

// [rc4l] The point a column-major view-projection maps to clip x = y = w = 0, i.e. its camera; false if there is none.
bool ComputeEyeFromViewProjection(const float mvp[16], float eye[3]);

// [rc4l] False only when all four corners sit outside one clip plane (Vulkan 0..w depth), so a visible quad is never dropped.
bool ComputeQuadMayBeVisible(const float mvp[16], const float corners[4][3]);

}} // namespace zx::horizon

#endif // ZX_HORIZON_COMPUTE_H
