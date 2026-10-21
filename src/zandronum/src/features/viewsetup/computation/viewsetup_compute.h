// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The view's numbers as FGLRenderer::RenderViewpoint and FrustumAngle work them out: the pitch stretched by the level's pixel ratio, and the half-angle the walks are seeded with.

#ifndef ZX_VIEWSETUP_COMPUTE_H
#define ZX_VIEWSETUP_COMPUTE_H

namespace zx { namespace viewsetup {

// [rc4l] viewpitch (BAM) as a pitch in degrees once the level's pixel stretch is applied, as the playsim's 1:1 pitch looks on stretched pixels.
float ComputeStretchedPitch(unsigned viewPitch, float pixelStretch);

// [rc4l] What the walks' frustum half-angle is worked out from: the view's stretched pitch and roll and the lens's field of view across in degrees, its width over height and fov ratio, and GL's aspect multiplier.
struct FrustumIn { float pitchDeg, rollDeg, fovDeg, ratio, fovRatio; int aspectMultiplier; };

// [rc4l] FrustumAngle: half the angle the view can see either side of its heading, GL's estimate or the widest heading through the screen's corners if that is wider, or 2^32-1 for a view that sees all round.
unsigned ComputeFrustumAngle(const FrustumIn &in);

// [rc4l] How far a sky layer has scrolled at the frame's clock, in degrees, as RenderViewpoint keeps mSky1Pos and mSky2Pos.
float ComputeSkyScroll(long frameMS, float skySpeed);

// [rc4l] SetViewAngle: the yaw GL's matrices take for a view angle (BAM), and the unit vector it faces in the plane.
void ComputeViewAngleVector(unsigned viewAngle, float &yaw, float &vectorX, float &vectorY);

// [rc4l] RenderView's fov ratio: 4:3 unless the screen is taller than wide, when it is the screen's own.
float ComputeFovRatio(float ratio, bool tallerThanWide);

// [rc4l] gl_colormap.h's and colormaps.h's values the fixed colormap choice reads (viewsetup.cpp asserts they match).
enum { kCmDefault = 0, kCmFirstSpecial = 1, kCmFogLayer = 0x10000000, kCmLite = 0x20000000, kCmTorch = 0x20000010, kNoFixedColormap = -1, kInverseColormap = 0 };
// [rc4l] The light power a player carries, the last in the inventory winning.
enum { kNoLightPower = 0, kTorchPower = 1, kLightAmpPower = 2 };

struct FixedColormapIn
{
	bool hasPlayer;        // [rc4l] the camera is a player
	bool inverse;          // [rc4l] extralight is INT_MIN, invulnerability's inverse map
	int  fixedColormap;    // [rc4l] the player's, or kNoFixedColormap
	int  fixedLightLevel;  // [rc4l] -1 for none
	int  lightPower;       // [rc4l] k*Power
};

// [rc4l] SetFixedColormap's choice: the inverse map, the player's fixed one, or a light amplifier's.
int ComputeFixedColormap(const FixedColormapIn &in);

// [rc4l] What main.fp does to a texel under a fixed colormap: nothing, map its grey into a special colormap's ramp, multiply it by a colour, or draw a sprite's fog layer.
enum { kShadeNone = 0, kShadeRamp = 1, kShadeTint = 2, kShadeFogLayer = 3 };

struct FixedColormapShade
{
	int   mode;       // [rc4l] a kShade* value, uFixedColormap
	float start[3];   // [rc4l] the ramp's colour at black, or the tint, uFixedColormapStart
	float range[3];   // [rc4l] the ramp from black to white, uFixedColormapRange
};

// [rc4l] FRenderState::ApplyShader's uniforms for `colormap`: a special colormap's ramp from `colorizeStart` to `colorizeEnd` (read only when it names one of `specialCount`), light amplification's white or night-vision green, or a torch's flicker.
FixedColormapShade ComputeFixedColormapShade(int colormap, int specialCount, const float colorizeStart[3], const float colorizeEnd[3], bool enhancedNightvision);

// [rc4l] Whether the view is the player's own eye seeing its body from outside: the chase camera (consoleplayer's, never in free spectate) or a dead player's death camera on the player's own body.
bool ComputeSeesOwnBody(bool cameraIsPlayer, bool freeSpectate, bool isConsolePlayer, bool consoleSpectating, bool chaseCam,
	bool deathCamera, bool dead, bool cameraIsPlayerBody);

// [rc4l] GetMaxViewPitch: Zandronum's software limits (56 down, 56 up or 32 with the old freelook limit) when forced, else maxviewpitch.
int ComputeMaxViewPitch(bool down, bool softwareLimits, bool oldFreelookLimit, int maxViewPitch);

// [rc4l] GL's area_t, where the eye is against a Boom height transfer.
enum { kAreaNormal = 0, kAreaBelow = 1, kAreaAbove = 2, kAreaDefault = 3 };

// [rc4l] SetViewArea: below the transfer's floor, above its ceiling (unless it fakes only a floor), between, or no transfer at all (left to the walk to find).
int ComputeViewArea(bool transfer, long long viewZ, long long transferFloorZ, long long transferCeilingZ, bool fakeFloorOnly);

// [rc4l] SetViewArea keeps the eye four units inside its sector's ceiling and floor, the floor winning where they cross; raw 16.16.
long long ComputeClampedViewZ(long long viewZ, long long floorZ, long long ceilingZ);

}} // namespace zx::viewsetup

#endif // ZX_VIEWSETUP_COMPUTE_H
