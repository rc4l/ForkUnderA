// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spritequad_compute.h"

#include <math.h>
#include <string.h>

namespace zx { namespace sprites {

SpriteRect ComputeSpriteRect(const SpriteRect &frame, float ul, float ur, bool mirror, float scaleX, float scaleY,
	float &uLeft, float &uRight)
{
	SpriteRect r = frame;
	if (mirror)
	{
		r.left = -r.width - r.left;
		uLeft = ul; uRight = ur;
	}
	else
	{
		uLeft = ur; uRight = ul;
	}
	// [rc4l] FloatRect::Scale.
	r.left *= scaleX; r.width *= scaleX;
	r.top *= scaleY; r.height *= scaleY;
	return r;
}

SpriteEdges ComputeSpriteEdges(float x, float y, float z, const SpriteRect &r, bool wallSprite, float dirX, float dirY)
{
	const float rightfac = -r.left;
	const float leftfac = rightfac - r.width;
	SpriteEdges e;
	e.z1 = z - r.top;
	e.z2 = e.z1 - r.height;
	if (!wallSprite)
	{
		e.x1 = x - dirY * leftfac;
		e.x2 = x - dirY * rightfac;
		e.y1 = y + dirX * leftfac;
		e.y2 = y + dirX * rightfac;
	}
	else
	{
		e.x1 = x + dirY * leftfac;
		e.x2 = x + dirY * rightfac;
		e.y1 = y - dirX * leftfac;
		e.y2 = y - dirX * rightfac;
	}
	return e;
}

// [rc4l] GL's Matrix3x4 (gl_geometric.h), operation for operation, so the corners come out bit for bit.
struct M34
{
	float m[3][4];

	void Identity() { memset(m, 0, sizeof(m)); m[0][0] = m[1][1] = m[2][2] = 1.f; }

	void Translate(float x, float y, float z)
	{
		m[0][3] = m[0][0]*x + m[0][1]*y + m[0][2]*z + m[0][3];
		m[1][3] = m[1][0]*x + m[1][1]*y + m[1][2]*z + m[1][3];
		m[2][3] = m[2][0]*x + m[2][1]*y + m[2][2]*z + m[2][3];
	}

	M34 operator*(const M34 &o) const
	{
		M34 r;
		for (int i = 0; i < 3; i++)
		{
			r.m[i][0] = m[i][0]*o.m[0][0] + m[i][1]*o.m[1][0] + m[i][2]*o.m[2][0];
			r.m[i][1] = m[i][0]*o.m[0][1] + m[i][1]*o.m[1][1] + m[i][2]*o.m[2][1];
			r.m[i][2] = m[i][0]*o.m[0][2] + m[i][1]*o.m[1][2] + m[i][2]*o.m[2][2];
			r.m[i][3] = m[i][0]*o.m[0][3] + m[i][1]*o.m[1][3] + m[i][2]*o.m[2][3] + m[i][3];
		}
		return r;
	}

	void Rotate(float ax, float ay, float az, float angle)
	{
		// [rc4l] Vector::Normalize: one over the float length, then a multiply each.
		const float l = 1.f / sqrtf((ax * ax) + (ay * ay) + (az * az));
		ax = ax * l; ay = ay * l; az = az * l;
		const double c = cos(angle * 3.14159265358979323846 / 180.), s = sin(angle * 3.14159265358979323846 / 180.), t = 1 - c;
		const double sx = s * ax, sy = s * ay, sz = s * az;
		double tx, ty, txx, tyy, u, v;
		M34 m1;
		tx = t * ax;
		m1.m[0][0] = float((txx = tx * ax) + c);
		m1.m[0][1] = float((u = tx * ay) - sz);
		m1.m[0][2] = float((v = tx * az) + sy);
		ty = t * ay;
		m1.m[1][0] = float(u + sz);
		m1.m[1][1] = float((tyy = ty * ay) + c);
		m1.m[1][2] = float((u = ty * az) - sx);
		m1.m[2][0] = float(v - sy);
		m1.m[2][1] = float(u + sx);
		m1.m[2][2] = float((t - txx - tyy) + c);
		m1.m[0][3] = m1.m[1][3] = m1.m[2][3] = 0.f;
		*this = (*this) * m1;
	}

	void Apply(float x, float y, float z, float out[3]) const
	{
		out[0] = x*m[0][0] + y*m[0][1] + z*m[0][2] + m[0][3];
		out[1] = x*m[1][0] + y*m[1][1] + z*m[1][2] + m[1][3];
		out[2] = x*m[2][0] + y*m[2][1] + z*m[2][2] + m[2][3];
	}
};

static void Corner(float out[3], float a, float b, float c) { out[0] = a; out[1] = b; out[2] = c; }

void ComputeSpriteCorners(const SpriteEdges &e, bool xyBillboard, bool roll, float viewYaw, float viewPitch,
	float rollDegrees, float out[4][3])
{
	if (!xyBillboard && !roll)
	{
		Corner(out[0], e.x1, e.z1, e.y1);
		Corner(out[1], e.x2, e.z1, e.y2);
		Corner(out[2], e.x1, e.z2, e.y1);
		Corner(out[3], e.x2, e.z2, e.y2);
		return;
	}
	const float xc = (float)((e.x1 + e.x2) * 0.5);
	const float yc = (float)((e.y1 + e.y2) * 0.5);
	const float zc = (float)((e.z1 + e.z2) * 0.5);
	// [rc4l] DEG2RAD, whose PI is a double and whose divisor a float.
	const float angleRad = (float)((270. - viewYaw) * 3.14159265358979323846 / 180.f);
	M34 mat;
	mat.Identity();
	mat.Translate(xc, zc, yc);
	// [rc4l] The float sin and cos, which is what GLSprite::Draw's call on a float resolves to; the double ones differ in the last bit.
	if (xyBillboard) mat.Rotate(-sinf(angleRad), 0, cosf(angleRad), -viewPitch);
	if (roll) mat.Rotate(cosf(angleRad), 0, sinf(angleRad), rollDegrees);
	mat.Translate(-xc, -zc, -yc);
	mat.Apply(e.x1, e.z1, e.y1, out[0]);
	mat.Apply(e.x2, e.z1, e.y2, out[1]);
	mat.Apply(e.x1, e.z2, e.y1, out[2]);
	mat.Apply(e.x2, e.z2, e.y2, out[3]);
}

}} // namespace zx::sprites
