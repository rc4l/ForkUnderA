// [rc4l] Implementation of the 4x4 matrix helpers, column-major throughout.
#include "features/hwrender/computation/matrix_compute.h"
#include <cmath>

namespace hwrender
{

Mat4 ComputeIdentity()
{
	Mat4 r = {};
	r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
	return r;
}

Mat4 ComputeMultiply(const Mat4 &a, const Mat4 &b)
{
	Mat4 r = {};
	for (int col = 0; col < 4; col++)
	{
		for (int row = 0; row < 4; row++)
		{
			float sum = 0.0f;
			for (int k = 0; k < 4; k++)
				sum += a.m[k * 4 + row] * b.m[col * 4 + k];
			r.m[col * 4 + row] = sum;
		}
	}
	return r;
}

Mat4 ComputeTranspose(const Mat4 &a)
{
	Mat4 r = {};
	for (int col = 0; col < 4; col++)
		for (int row = 0; row < 4; row++)
			r.m[col * 4 + row] = a.m[row * 4 + col];
	return r;
}

Mat4 ComputeTranslation(float x, float y, float z)
{
	Mat4 r = ComputeIdentity();
	r.m[12] = x;
	r.m[13] = y;
	r.m[14] = z;
	return r;
}

Mat4 ComputeScale(float x, float y, float z)
{
	Mat4 r = {};
	r.m[0] = x;
	r.m[5] = y;
	r.m[10] = z;
	r.m[15] = 1.0f;
	return r;
}

Mat4 ComputeRotation(float angleDegrees, float x, float y, float z)
{
	const float len = std::sqrt(x * x + y * y + z * z);
	if (len == 0.0f)
		return ComputeIdentity();

	x /= len;
	y /= len;
	z /= len;

	const float rad = angleDegrees * (3.14159265358979323846f / 180.0f);
	const float c = std::cos(rad);
	const float s = std::sin(rad);
	const float t = 1.0f - c;

	Mat4 r = {};
	// [rc4l] Standard axis-angle (Rodrigues) rotation, written column-major.
	r.m[0]  = t * x * x + c;
	r.m[1]  = t * x * y + s * z;
	r.m[2]  = t * x * z - s * y;
	r.m[4]  = t * x * y - s * z;
	r.m[5]  = t * y * y + c;
	r.m[6]  = t * y * z + s * x;
	r.m[8]  = t * x * z + s * y;
	r.m[9]  = t * y * z - s * x;
	r.m[10] = t * z * z + c;
	r.m[15] = 1.0f;
	return r;
}

Mat4 ComputeOrtho(float l, float r, float b, float t, float n, float f)
{
	Mat4 out = {};
	out.m[0]  = 2.0f / (r - l);
	out.m[5]  = 2.0f / (t - b);
	out.m[10] = -2.0f / (f - n);
	out.m[12] = -(r + l) / (r - l);
	out.m[13] = -(t + b) / (t - b);
	out.m[14] = -(f + n) / (f - n);
	out.m[15] = 1.0f;
	return out;
}

Mat4 ComputePerspective(float fovYDegrees, float aspect, float n, float f)
{
	const float rad = fovYDegrees * (3.14159265358979323846f / 180.0f);
	const float focal = 1.0f / std::tan(rad * 0.5f);

	Mat4 out = {};
	out.m[0]  = focal / aspect;
	out.m[5]  = focal;
	out.m[10] = (f + n) / (n - f);
	out.m[11] = -1.0f;
	out.m[14] = (2.0f * f * n) / (n - f);
	return out;
}

} // namespace hwrender
