// [rc4l] Reproduces the legacy renderer's fixed-function view/projection setup as explicit matrices,
// since core profile removes glMatrixMode/gluPerspective. Mirrors FGLRenderer::SetProjection and
// SetViewMatrix in gl/scene/gl_scene.cpp exactly, including the mirror and plane-mirror flips.
#pragma once

#include "matrix_compute.h"

namespace hwrender
{

// [rc4l] Vertical FOV the legacy path derives from the horizontal FOV and its aspect correction.
float ComputeSceneFovY(float fovDegrees, float fovRatio);

// [rc4l] Projection matching gluPerspective(fovy, ratio, 5, 65536).
Mat4 ComputeSceneProjection(float fovDegrees, float ratio, float fovRatio);

// [rc4l] View matrix; camera position is in Doom units (X east, Y north, Z up).
Mat4 ComputeSceneView(float roll, float pitch, float yaw,
	float camX, float camY, float camZ, bool mirror, bool planeMirror);

} // namespace hwrender
