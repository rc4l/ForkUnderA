// [rc4l] Implementation of frustum extraction and culling; column-major element at row r, col c is m[c*4+r].
#include "features/hwrender/computation/frustum_compute.h"
#include <cmath>

namespace hwrender
{

namespace
{
// [rc4l] Normalize a plane by the length of its (a,b,c) normal.
Plane Normalize(float a, float b, float c, float d)
{
	const float len = std::sqrt(a * a + b * b + c * c);
	if (len == 0.0f)
		return Plane{a, b, c, d};
	const float inv = 1.0f / len;
	return Plane{a * inv, b * inv, c * inv, d * inv};
}
} // namespace

Frustum ComputeFrustumPlanes(const Mat4 &m)
{
	// [rc4l] Matrix rows (column-major access).
	const float r0[4] = {m.m[0], m.m[4], m.m[8], m.m[12]};
	const float r1[4] = {m.m[1], m.m[5], m.m[9], m.m[13]};
	const float r2[4] = {m.m[2], m.m[6], m.m[10], m.m[14]};
	const float r3[4] = {m.m[3], m.m[7], m.m[11], m.m[15]};

	Frustum f;
	f.planes[0] = Normalize(r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]); // left
	f.planes[1] = Normalize(r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]); // right
	f.planes[2] = Normalize(r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]); // bottom
	f.planes[3] = Normalize(r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]); // top
	f.planes[4] = Normalize(r3[0] + r2[0], r3[1] + r2[1], r3[2] + r2[2], r3[3] + r2[3]); // near
	f.planes[5] = Normalize(r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]); // far
	return f;
}

float ComputePlaneDistance(const Plane &p, float x, float y, float z)
{
	return p.a * x + p.b * y + p.c * z + p.d;
}

bool ComputeSphereInFrustum(const Frustum &f, float x, float y, float z, float radius)
{
	for (int i = 0; i < 6; i++)
	{
		if (ComputePlaneDistance(f.planes[i], x, y, z) < -radius)
			return false; // fully outside this plane
	}
	return true;
}

} // namespace hwrender
