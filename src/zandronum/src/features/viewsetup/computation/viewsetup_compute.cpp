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

unsigned ComputeFrustumAngle(float pitchDeg, float fovDeg, int aspectMultiplier)
{
	const float tilt = fabsf(pitchDeg);
	if (tilt > 46.0f) return 0xffffffffu;
	const double angle = 2.0 + (45.0 + (tilt / 1.9)) * fovDeg * 48.0 / aspectMultiplier / 90.0;
	const unsigned a1 = ComputeXsRoundToUInt(angle / 180. * 2147483648.0);
	return a1 >= 0x80000000u ? 0xffffffffu : a1;
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
