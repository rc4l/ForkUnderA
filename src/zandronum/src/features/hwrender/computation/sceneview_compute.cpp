// [rc4l] See sceneview_compute.h.
#include "sceneview_compute.h"

#include <math.h>

namespace hwrender
{

namespace
{
const float kPi = 3.14159265358979323846f;
const float kNearPlane = 5.0f;
const float kFarPlane = 65536.0f;
}

float ComputeSceneFovY(float fovDegrees, float fovRatio)
{
	// [rc4l] Guard the degenerate ratio; the legacy code assumes callers never pass zero.
	if (fovRatio == 0.0f) return fovDegrees;
	const float halfRad = (fovDegrees * kPi / 180.0f) * 0.5f;
	return 2.0f * (atanf(tanf(halfRad) / fovRatio) * 180.0f / kPi);
}

Mat4 ComputeSceneProjection(float fovDegrees, float ratio, float fovRatio)
{
	return ComputePerspective(ComputeSceneFovY(fovDegrees, fovRatio), ratio, kNearPlane, kFarPlane);
}

Mat4 ComputeSceneView(float roll, float pitch, float yaw,
	float camX, float camY, float camZ, bool mirror, bool planeMirror)
{
	// [rc4l] The legacy path builds this with successive glRotate/glTranslate/glScale calls, which
	// post-multiply; ComputeMultiply(a, b) is a*b on a column vector, so the order carries over as-is.
	const float mult = mirror ? -1.0f : 1.0f;
	const float planeMult = planeMirror ? -1.0f : 1.0f;

	Mat4 m = ComputeRotation(roll, 0.0f, 0.0f, 1.0f);
	m = ComputeMultiply(m, ComputeRotation(pitch, 1.0f, 0.0f, 0.0f));
	m = ComputeMultiply(m, ComputeRotation(yaw, 0.0f, mult, 0.0f));
	// [rc4l] Doom's Z (up) becomes GL's Y, and Doom's Y (north) becomes GL's -Z.
	m = ComputeMultiply(m, ComputeTranslation(camX * mult, -camZ * planeMult, -camY));
	m = ComputeMultiply(m, ComputeScale(-mult, planeMult, 1.0f));
	return m;
}

} // namespace hwrender
