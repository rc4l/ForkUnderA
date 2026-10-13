// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#define _USE_MATH_DEFINES
#include "features/sprites/computation/modelpose_compute.h"
#include "features/fixed64/computation/xsround_compute.h"

#include <cmath>
#include <string.h>

namespace zx { namespace sprites {

namespace {

const double kPi = 3.14159265358979323846;

// [rc4l] VSMatrix::multMatrix: m = m * a, summed in VSMatrix's order.
void Multiply(float *m, const float *a)
{
	float res[16];
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
		{
			res[j * 4 + i] = 0.0f;
			for (int k = 0; k < 4; ++k) res[j * 4 + i] += m[k * 4 + i] * a[j * 4 + k];
		}
	memcpy(m, res, sizeof(res));
}

void Identity(float *m)
{
	for (int i = 0; i < 16; ++i) m[i] = 0.0f;
	for (int i = 0; i < 4; ++i) m[i * 5] = 1.0f;
}

void Translate(float *m, float x, float y, float z)
{
	float t[16];
	Identity(t);
	t[12] = x; t[13] = y; t[14] = z;
	Multiply(m, t);
}

void Scale(float *m, float x, float y, float z)
{
	float s[16];
	Identity(s);
	s[0] = x; s[5] = y; s[10] = z;
	Multiply(m, s);
}

// [rc4l] VSMatrix::rotate, its axis normalised first.
void Rotate(float *m, float angle, float x, float y, float z)
{
	const float rad = (float)(angle * (kPi / 180.0f));
	const float co = std::cos(rad), si = std::sin(rad);
	const float mag = std::sqrt(x * x + y * y + z * z);
	const float v[3] = { x / mag, y / mag, z / mag };
	float r[16];
	r[0] = co + v[0] * v[0] * (1 - co);
	r[4] = v[0] * v[1] * (1 - co) - v[2] * si;
	r[8] = v[0] * v[2] * (1 - co) + v[1] * si;
	r[12] = 0.0f;
	r[1] = v[0] * v[1] * (1 - co) + v[2] * si;
	r[5] = co + v[1] * v[1] * (1 - co);
	r[9] = v[1] * v[2] * (1 - co) - v[0] * si;
	r[13] = 0.0f;
	r[2] = v[0] * v[2] * (1 - co) - v[1] * si;
	r[6] = v[1] * v[2] * (1 - co) + v[0] * si;
	r[10] = co + v[2] * v[2] * (1 - co);
	r[14] = 0.0f;
	r[3] = r[7] = r[11] = 0.0f;
	r[15] = 1.0f;
	Multiply(m, r);
}

} // namespace

void ComputeModelMatrix(const ModelPose &p, float out[16])
{
	Identity(out);
	Translate(out, p.position[0], p.position[1], p.position[2]);
	Rotate(out, p.yaw, 0, 1, 0);
	Rotate(out, p.pitch, 0, 0, 1);
	Rotate(out, p.roll, 1, 0, 0);
	if (p.rotating)
	{
		Translate(out, p.spinCenter[0], p.spinCenter[1], p.spinCenter[2]);
		Rotate(out, p.spin, p.spinAxis[0], p.spinAxis[1], p.spinAxis[2]);
		Translate(out, -p.spinCenter[0], -p.spinCenter[1], -p.spinCenter[2]);
	}
	Scale(out, p.scale[0], p.scale[1], p.scale[2]);
	Translate(out, p.offset[0], p.offset[1], p.offset[2]);
	Rotate(out, p.yawOffset, 0, 1, 0);
	Rotate(out, p.pitchOffset, 0, 0, 1);
	Rotate(out, p.rollOffset, 1, 0, 0);
	Scale(out, 1, p.stretch, 1);
}

void ComputeHUDModelMatrix(const HUDModelPose &p, float out[16])
{
	Identity(out);
	Scale(out, p.scale[0], p.scale[1], p.scale[2]);
	Translate(out, p.offset[0], p.offset[1], p.offset[2]);
	// [rc4l] The weapon's bob, like Doom's; then the quarter turn the jDoom models need, then the model's own.
	Rotate(out, p.bobX / 4, 0, 1, 0);
	Rotate(out, -p.bobY / 4, 1, 0, 0);
	Rotate(out, 90.f, 0, 1, 0);
	Rotate(out, -p.angleOffset, 0, 1, 0);
	Rotate(out, p.pitchOffset, 0, 0, 1);
	Rotate(out, -p.rollOffset, 1, 0, 0);
}

float ComputeMomentumPitch(double x, double y, double z, float angle)
{
	float pitch = 0;
	if (z || x || y) pitch = float(std::atan(z / std::sqrt(x * x + y * y)) / kPi * 180);
	// [rc4l] Moving backwards turns the pitch over; straight up or down it is the climb's size.
	if (x || y)
	{
		if ((x * std::cos(angle * kPi / 180) + y * std::sin(angle * kPi / 180)) / std::sqrt(x * x + y * y) < 0) pitch *= -1;
	}
	else pitch = std::fabs(pitch);
	return pitch;
}

float ComputeInheritedDegrees(long long raw)
{
	return float(static_cast<double>(raw >> 16) / (1 << 13) * 45 + static_cast<double>(raw & 0x0000FFFF) / (1 << 29) * 45);
}

float ComputeModelSpin(float rotationSpeed, float timeTics)
{
	const float time = rotationSpeed * timeTics / 200.f;
	return float((time - zx::ComputeXsFloor(time)) * 360.f);
}

float ComputeRollAgainstAngle(float pointAngle, float actorAngle, bool aboveEye)
{
	float diff = pointAngle - actorAngle;
	if (diff > 180) diff -= 360;
	else if (diff < -180) diff += 360;
	if (aboveEye) diff *= -1;
	if (diff < 90 && diff > -90) diff *= -1;
	return diff;
}

float ComputeAlignPitch(float dz, float distance)
{
	return (float)(std::atan2(dz, distance) * 180.f / kPi);
}

double ComputeModelInter(int stateTics, int ticsLeft, float ticFraction, bool doubledBefore, bool doubledAfter)
{
	// [rc4l] A state lasting longer than its own length (ZPoisonShroom) has not started to blend.
	if (stateTics < ticsLeft) return 0.;
	double inter = static_cast<double>(stateTics - ticsLeft + ticFraction) / static_cast<double>(stateTics);
	if (doubledBefore) inter = inter / 2. + 0.5;
	if (doubledAfter) inter /= 2.;
	return inter;
}

float ComputeModelSlotBlend(bool framesDiffer, bool interpolates, double inter)
{
	return framesDiffer && interpolates ? (float)inter : 0.f;
}

}} // namespace zx::sprites
