// [rc4l] Pure column-major 4x4 matrix math replacing the fixed-function GL matrix stack.
#ifndef ZX_HWRENDER_MATRIX_COMPUTE_H
#define ZX_HWRENDER_MATRIX_COMPUTE_H

namespace hwrender
{

// [rc4l] Column-major 4x4 matrix, laid out exactly as OpenGL expects a uniform matrix.
struct Mat4
{
	float m[16];
};

// [rc4l] The identity matrix.
Mat4 ComputeIdentity();

// [rc4l] Matrix product a*b (column-vector convention: (a*b)*v applies b to v, then a).
Mat4 ComputeMultiply(const Mat4 &a, const Mat4 &b);

// [rc4l] Transpose (row-major <-> column-major, or building a normal matrix).
Mat4 ComputeTranspose(const Mat4 &a);

// [rc4l] Translation matrix.
Mat4 ComputeTranslation(float x, float y, float z);

// [rc4l] Non-uniform scale matrix.
Mat4 ComputeScale(float x, float y, float z);

// [rc4l] Rotation of angleDegrees about axis (x,y,z). A zero-length axis yields identity.
Mat4 ComputeRotation(float angleDegrees, float x, float y, float z);

// [rc4l] Orthographic projection (maps [l,r]x[b,t]x[n,f] to the [-1,1] clip cube), like glOrtho.
Mat4 ComputeOrtho(float l, float r, float b, float t, float n, float f);

// [rc4l] Perspective projection, like gluPerspective (fovYDegrees is the full vertical FOV).
Mat4 ComputePerspective(float fovYDegrees, float aspect, float n, float f);

} // namespace hwrender

#endif // ZX_HWRENDER_MATRIX_COMPUTE_H
