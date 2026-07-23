// [rc4l] Pure view-frustum plane extraction (Gribb-Hartmann) plus sphere culling, for CPU-side rejection before submission.
#ifndef ZX_HWRENDER_FRUSTUM_COMPUTE_H
#define ZX_HWRENDER_FRUSTUM_COMPUTE_H

#include "features/hwrender/computation/matrix_compute.h"

namespace hwrender
{

// [rc4l] Plane a*x + b*y + c*z + d = 0, with (a,b,c) normalized and pointing to the inside.
struct Plane
{
	float a, b, c, d;
};

// [rc4l] The six clip planes, in order: left, right, bottom, top, near, far.
struct Frustum
{
	Plane planes[6];
};

// [rc4l] Extract the six frustum planes from a column-major view-projection matrix.
Frustum ComputeFrustumPlanes(const Mat4 &viewProj);

// [rc4l] Signed distance from a point to a plane (positive = inside half-space).
float ComputePlaneDistance(const Plane &p, float x, float y, float z);

// [rc4l] True if the sphere is at least partially inside the frustum (conservative).
bool ComputeSphereInFrustum(const Frustum &f, float x, float y, float z, float radius);

} // namespace hwrender

#endif // ZX_HWRENDER_FRUSTUM_COMPUTE_H
