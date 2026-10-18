// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/viewsetup/computation/viewsetup_compute.h"
#include "features/fixed64/computation/xsround_compute.h"

#include <math.h>

namespace zx { namespace viewsetup {

static const double kPi = 3.14159265358979323846;

float ComputeStretchedPitch(unsigned viewPitch, float pixelStretch)
{
	// [rc4l] bam2rad, then folded to a signed pitch and clamped to straight up or down.
	double radPitch = double(viewPitch >> 1) * (kPi / 1073741824.0);
	if (radPitch > kPi) radPitch -= 2 * kPi;
	if (radPitch < -kPi / 2) radPitch = -kPi / 2;
	else if (radPitch > kPi / 2) radPitch = kPi / 2;
	const double angx = cos(radPitch);
	const double angy = sin(radPitch) * pixelStretch;
	const double alen = sqrt(angx * angx + angy * angy);
	return (float)(asin(angy / alen) * 180.f / kPi);
}

// [rc4l] The widest heading either side, in degrees, of a ray through a corner of the lens's screen once rolled and pitched, or -1 when one points behind the eye.
static double CornerHeading(const FrustumIn &in)
{
	const double rad = kPi / 180.0;
	const double ty = tan(in.fovDeg * 0.5 * rad) / in.fovRatio, tx = ty * in.ratio;
	const double cp = cos(fabs(in.pitchDeg) * rad), sp = sin(fabs(in.pitchDeg) * rad);
	const double cr = cos(in.rollDeg * rad), sr = sin(in.rollDeg * rad);
	double widest = 0.0;
	for (int corner = 0; corner < 4; corner++)
	{
		const double x = (corner & 1) ? tx : -tx, y = (corner & 2) ? ty : -ty;
		const double across = x * cr - y * sr, up = x * sr + y * cr;
		const double ahead = cp - up * sp;
		if (ahead <= 0.0) return -1.0;
		const double heading = atan2(fabs(across), ahead) / rad;
		if (heading > widest) widest = heading;
	}
	return widest;
}

unsigned ComputeFrustumAngle(const FrustumIn &in)
{
	const float tilt = fabsf(in.pitchDeg);
	if (tilt > 46.0f || in.fovDeg >= 180.0f) return 0xffffffffu;
	const double corner = CornerHeading(in);
	if (corner < 0.0) return 0xffffffffu;
	// [rc4l] GL's estimate stays the least the angle may be, and the corners plus its 2 degree margin cover the 4:3 lens pitched past about 30 degrees it fell short on (queue #58).
	const double estimate = 2.0 + (45.0 + (tilt / 1.9)) * in.fovDeg * 48.0 / in.aspectMultiplier / 90.0;
	const double angle = estimate > corner + 2.0 ? estimate : corner + 2.0;
	const double bams = angle / 180. * 2147483648.0;
	return bams >= 2147483647.5 ? 0xffffffffu : ComputeXsRoundToUInt(bams);
}

float ComputeSkyScroll(long frameMS, float skySpeed)
{
	return fmodf(frameMS * skySpeed, 1024.f) * 90.f / 256.f;
}

void ComputeViewAngleVector(unsigned viewAngle, float &yaw, float &vectorX, float &vectorY)
{
	// [rc4l] The angle to fine-angle resolution, in degrees.
	const float angle = (float)(viewAngle >> 19) * 360.0f / 8192;
	yaw = 270.0f - angle;
	vectorX = (float)cos(angle * kPi / 180.f);
	vectorY = (float)sin(angle * kPi / 180.f);
}

int ComputeFixedColormap(const FixedColormapIn &in)
{
	if (!in.hasPlayer) return kCmDefault;
	if (in.inverse) return kCmFirstSpecial + kInverseColormap;
	if (in.fixedColormap != kNoFixedColormap) return kCmFirstSpecial + in.fixedColormap;
	if (in.fixedLightLevel == -1) return kCmDefault;
	if (in.lightPower == kTorchPower) return in.fixedLightLevel + kCmTorch;
	if (in.lightPower == kLightAmpPower) return kCmLite;
	return kCmDefault;
}

FixedColormapShade ComputeFixedColormapShade(int colormap, int specialCount, const float colorizeStart[3], const float colorizeEnd[3], bool enhancedNightvision)
{
	FixedColormapShade s = { kShadeNone, { 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f } };
	if (colormap >= kCmFirstSpecial && colormap < kCmFirstSpecial + specialCount)
	{
		s.mode = kShadeRamp;
		for (int i = 0; i < 3; i++) { s.start[i] = colorizeStart[i]; s.range[i] = colorizeEnd[i] - colorizeStart[i]; }
	}
	else if (colormap == kCmFogLayer) s.mode = kShadeFogLayer;
	else if (colormap == kCmLite)
	{
		s.mode = kShadeTint;
		s.start[0] = s.start[2] = enhancedNightvision ? 0.375f : 1.f;
		s.start[1] = 1.f;
	}
	else if (colormap >= kCmTorch)
	{
		// [rc4l] A torch's light level, 0 to 7 as APowerTorch keeps it, flickers it between 0.9 and 0.8, its blue lower in night vision.
		const float r = 0.8f + (7 - (colormap - kCmTorch)) / 70.0f;
		s.mode = kShadeTint;
		s.start[0] = s.start[1] = r;
		s.start[2] = enhancedNightvision ? r * 0.75f : r;
	}
	return s;
}

bool ComputeSeesOwnBody(bool cameraIsPlayer, bool freeSpectate, bool isConsolePlayer, bool consoleSpectating, bool chaseCam,
	bool deathCamera, bool dead, bool cameraIsPlayerBody)
{
	return cameraIsPlayer && !freeSpectate && (!isConsolePlayer || !consoleSpectating) && (chaseCam || (deathCamera && dead)) && cameraIsPlayerBody;
}

int ComputeMaxViewPitch(bool down, bool softwareLimits, bool oldFreelookLimit, int maxViewPitch)
{
	if (!softwareLimits) return maxViewPitch;
	return down ? 56 : (oldFreelookLimit ? 32 : 56);
}

float ComputeFovRatio(float ratio, bool tallerThanWide)
{
	return tallerThanWide ? ratio : 1.333333f;
}

int ComputeViewArea(bool transfer, long long viewZ, long long transferFloorZ, long long transferCeilingZ, bool fakeFloorOnly)
{
	if (!transfer) return kAreaDefault;
	if (viewZ <= transferFloorZ) return kAreaBelow;
	return (viewZ > transferCeilingZ && !fakeFloorOnly) ? kAreaAbove : kAreaNormal;
}

long long ComputeClampedViewZ(long long viewZ, long long floorZ, long long ceilingZ)
{
	const long long unit4 = 4 << 16;
	if (viewZ > ceilingZ - unit4) viewZ = ceilingZ - unit4;
	if (viewZ < floorZ + unit4) viewZ = floorZ + unit4;
	return viewZ;
}

}} // namespace zx::viewsetup
