// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's view-projection, built the way GL's FGLRenderer::SetViewMatrix and SetProjection build theirs, in the mesh's (x, height, y) space with Vulkan's 0..1 depth.

#ifndef ZX_BACKENDVIEW_COMPUTE_H
#define ZX_BACKENDVIEW_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] Where the eye is (map x, y, height), which way it looks (yaw from east, pitch as viewpitch gives it, both radians), and the lens.
struct BackendCamera
{
	float x, y, z;
	float yaw, pitch;
	float pixelStretch;   // [rc4l] glset.pixelstretch: Doom's pixels are this much taller than wide
	float fovY, aspect;   // [rc4l] the vertical field of view in radians, of the stretched world, and width over height
};

// [rc4l] The column-major view alone, GL's ViewMatrix: mesh space into eye space, heights stretched by the pixel aspect before the pitch turns them.
void ComputeBackendView(const BackendCamera &c, float out[16]);

// [rc4l] The column-major view-projection for a camera: ComputeBackendView under GL's projection with Vulkan's depth, so every view keeps GL's framing.
void ComputeBackendMVP(const BackendCamera &c, float out[16]);

// [rc4l] a times b, column-major; out may not be either.
void ComputeMultiplyMatrix4(const float *a, const float *b, float *out);

// [rc4l] The inverse of a column-major 4x4, false when it has none.
bool ComputeInvertMatrix4(const float *m, float *out);

// [rc4l] What takes a matrix into a camera's eye space, as GL's HUD model loads its own in place of the view, to mesh space instead: the camera's view undone after it.
bool ComputeEyeToMesh(const BackendCamera &c, const float *eye, float *out);

// [rc4l] A rectangle of the screen in top-down pixels.
struct ScreenRect
{
	int x, y, w, h;
};

// [rc4l] Where GL's SetViewport puts the player's view: viewW wide at the view window's left, and the screen's height (a status bar's screenblocks) or screenblocks tenths of it, centred on the view window's rows.
ScreenRect ComputeViewWindowViewport(int screenH, int screenblocks, int windowX, int windowY, int viewW, int viewH);

// [rc4l] A clip-space scale and offset, x and y, that stand in for a viewport.
struct NdcWindow
{
	float sx, sy, ox, oy;
};

// [rc4l] What draws a projection into rect of a screenW by screenH target as a viewport there would, with device y up.
NdcWindow ComputeNdcWindow(const ScreenRect &rect, int screenW, int screenH);

// [rc4l] Put a column-major view-projection through w, so it lands where the viewport would put it while the target stays whole and gl_FragCoord keeps meaning the same pixel.
void ComputeApplyNdcWindow(const NdcWindow &w, float m[16]);

}} // namespace zx::hwrender

#endif // ZX_BACKENDVIEW_COMPUTE_H
