// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/spritepack_compute.h"

#include <cmath>
#include <cstring>

namespace zx { namespace hwrender {

namespace {

unsigned Bits(float f) { unsigned u; std::memcpy(&u, &f, 4); return u; }
float Float(unsigned u) { float f; std::memcpy(&f, &u, 4); return f; }

// [rc4l] x as an 8-bit step that reads back as x exactly, as a colour made from bytes does.
bool Byte(float x, unsigned &out)
{
	if (!(x >= 0.f && x <= 1.f)) return false;
	const float q = std::nearbyint(x * 255.f);
	if (q / 255.f != x) return false;
	out = (unsigned)q;
	return true;
}

// [rc4l] x as a whole number from lo to hi.
bool Whole(float x, int lo, int hi, int &out)
{
	if (!(x >= (float)lo && x <= (float)hi) || x != std::floor(x)) return false;
	out = (int)x;
	return true;
}

bool AllZero(const float *f, int n)
{
	for (int i = 0; i < n; i++) if (f[i] != 0.f) return false;
	return true;
}

// [rc4l] The two values a quad's corners take on one texture axis, false when they take more.
bool TwoValues(const float v[4], float &a, float &b)
{
	a = b = v[0];
	for (int i = 1; i < 4; i++)
	{
		if (v[i] == a || v[i] == b) continue;
		if (a != b) return false;
		b = v[i];
	}
	return true;
}

} // namespace

void ComputeSpriteMotionCorners(const SpriteCorner in[4], const SpriteMotion &m, float frac, float perpX, float perpY, float pitchDeg, SpriteCorner out[4])
{
	const float ang = -pitchDeg * 3.14159265358979323846f / 180.f;
	ComputeSpriteMotionCornersCS(in, m, frac, perpX, perpY, std::cos(ang), std::sin(ang), out);
}

void ComputeSpriteMotionCornersCS(const SpriteCorner in[4], const SpriteMotion &m, float frac, float perpX, float perpY, float cs, float sn, SpriteCorner out[4])
{
	const float back = 1.f - frac;
	const float sx = m.dx * back, sh = m.dh * back, sy = m.dy * back;
	// [Doruk-Aksoy] One point turned about the tic centre and slid, as the shader does a corner and the middle.
	auto turn = [&](float x, float y, float z, SpriteCorner &o) {
		const float t = (x - m.cx) * m.px + (z - m.cy) * m.py;
		o.x = m.cx + perpX * t - sx;
		o.y = y - sh;
		o.z = m.cy + perpY * t - sy;
	};
	for (int k = 0; k < 4; k++)
	{
		turn(in[k].x, in[k].y, in[k].z, out[k]);
		out[k].u = in[k].u;
		out[k].v = in[k].v;
	}
	if (!m.xy) return;
	SpriteCorner mid = {};
	turn((in[0].x + in[1].x + in[2].x + in[3].x) * 0.25f, (in[0].y + in[1].y + in[2].y + in[3].y) * 0.25f,
		(in[0].z + in[1].z + in[2].z + in[3].z) * 0.25f, mid);
	const float ax = perpX, az = perpY;
	for (int k = 0; k < 4; k++)
	{
		const float vx = out[k].x - mid.x, vy = out[k].y - mid.y, vz = out[k].z - mid.z;
		const float d = ax * vx + az * vz;
		out[k].x = mid.x + vx * cs + (-az * vy) * sn + ax * d * (1.f - cs);
		out[k].y = mid.y + vy * cs + (az * vx - ax * vz) * sn;
		out[k].z = mid.z + vz * cs + (ax * vy) * sn + az * d * (1.f - cs);
	}
}

bool ComputeSpritePack(const ScenePieceData &pd, const SpriteCorner corners[4], unsigned out[kSpriteWords], const SpriteMotion *motion)
{
	const float flood[8] = { pd.floodUx, pd.floodUy, pd.floodU0, pd.floodZ, pd.floodVx, pd.floodVy, pd.floodV0, pd.flood };
	const float normal[3] = { pd.nx, pd.ny, pd.nz };
	const float rest[6] = { pd.stackId, pd.animates, pd.plain, pd.scrollU, pd.scrollV, pd.warpStyle };
	if (!AllZero(flood, 8) || !AllZero(normal, 3) || !AllZero(rest, 6) || pd.warpSpeed != 1.f) return false;
	if (!AllZero(pd.glowTop, 4) || !AllZero(pd.glowBottom, 4) || !AllZero(pd.glowTopPlane, 4) || !AllZero(pd.glowBottomPlane, 4)) return false;
	if (!AllZero(pd.tint, 4) || !AllZero(pd.tintRange, 4)) return false;
	const float user[4] = { pd.actorRow, pd.user1, pd.user2, pd.user3 };
	if (!AllZero(user, 4)) return false;
	unsigned r, g, b, fr, fg, fb;
	if (!Byte(pd.r, r) || !Byte(pd.g, g) || !Byte(pd.b, b) || !Byte(pd.fogR, fr) || !Byte(pd.fogG, fg) || !Byte(pd.fogB, fb)) return false;
	int fogMode, mat, bright, texFlags, cpuLight;
	if (!Whole(pd.fogMode, -128, 127, fogMode) || !Whole(pd.matSlot, 0, 0xffff, mat) || !Whole(pd.brightSlot, 0, 0xffff, bright) ||
	    !Whole(pd.texFlags, 0, 0xffff, texFlags) || !Whole(pd.cpuLight, 0, 1, cpuLight)) return false;
	const float us[4] = { corners[0].u, corners[1].u, corners[2].u, corners[3].u };
	const float vs[4] = { corners[0].v, corners[1].v, corners[2].v, corners[3].v };
	float uA, uB, vA, vB;
	if (!TwoValues(us, uA, uB) || !TwoValues(vs, vA, vB)) return false;
	unsigned sel = 0;
	for (int k = 0; k < 4; k++)
	{
		out[k * 4 + 0] = Bits(corners[k].x);
		out[k * 4 + 1] = Bits(corners[k].y);
		out[k * 4 + 2] = Bits(corners[k].z);
		if (us[k] != uA) sel |= 1u << (2 * k);
		if (vs[k] != vA) sel |= 2u << (2 * k);
	}
	out[3] = Bits(uA); out[7] = Bits(uB); out[11] = Bits(vA); out[15] = Bits(vB);
	out[16] = r | (g << 8) | (b << 16) | (sel << 24);
	out[17] = fr | (fg << 8) | (fb << 16) | ((unsigned)(fogMode & 0xff) << 24);
	out[18] = (unsigned)mat | ((unsigned)bright << 16);
	out[19] = (unsigned)texFlags | ((unsigned)cpuLight << 16);
	out[20] = Bits(pd.softLight);
	out[21] = Bits(pd.fogDensity);
	out[22] = Bits(pd.alpha);
	out[23] = Bits(pd.desaturation);
	out[24] = Bits(pd.actorR);
	out[25] = Bits(pd.actorG);
	out[26] = Bits(pd.actorB);
	out[27] = motion == nullptr ? 0u : motion->xy ? 3u : 1u;
	const float m[7] = { motion ? motion->cx : 0.f, motion ? motion->cy : 0.f, motion ? motion->px : 0.f, motion ? motion->py : 0.f,
		motion ? motion->dx : 0.f, motion ? motion->dh : 0.f, motion ? motion->dy : 0.f };
	for (int i = 0; i < 7; i++) out[28 + i] = Bits(m[i]);
	out[35] = 0;
	return true;
}

void ComputeSpriteUnpack(const unsigned in[kSpriteWords], int corner, ScenePieceData &pd, SpriteCorner &c)
{
	pd = ScenePieceData();
	const unsigned sel = in[16] >> 24;
	c.x = Float(in[corner * 4 + 0]);
	c.y = Float(in[corner * 4 + 1]);
	c.z = Float(in[corner * 4 + 2]);
	c.u = Float((sel >> (2 * corner)) & 1u ? in[7] : in[3]);
	c.v = Float((sel >> (2 * corner)) & 2u ? in[15] : in[11]);
	pd.r = (in[16] & 0xff) / 255.f;
	pd.g = ((in[16] >> 8) & 0xff) / 255.f;
	pd.b = ((in[16] >> 16) & 0xff) / 255.f;
	pd.softLight = Float(in[20]);
	pd.fogDensity = Float(in[21]);
	pd.alpha = Float(in[22]);
	pd.fogMode = (float)(signed char)(in[17] >> 24);
	pd.matSlot = (float)(in[18] & 0xffff);
	pd.fogR = (in[17] & 0xff) / 255.f;
	pd.fogG = ((in[17] >> 8) & 0xff) / 255.f;
	pd.fogB = ((in[17] >> 16) & 0xff) / 255.f;
	pd.warpSpeed = 1.f;
	pd.actorR = Float(in[24]);
	pd.actorG = Float(in[25]);
	pd.actorB = Float(in[26]);
	pd.cpuLight = (float)((in[19] >> 16) & 1u);
	pd.brightSlot = (float)(in[18] >> 16);
	pd.desaturation = Float(in[23]);
	pd.texFlags = (float)(in[19] & 0xffff);
}

unsigned ComputeSpriteRef(unsigned wordOffset, int corner)
{
	return 0x80000000u | wordOffset | (unsigned)corner;
}

unsigned ComputeGeneralRef(unsigned index)
{
	return index & 0x7fffffffu;
}

bool ComputePassDrawsRun(unsigned run, unsigned ticRuns, bool cameraPass)
{
	return !cameraPass || run >= ticRuns;
}

}} // namespace zx::hwrender
