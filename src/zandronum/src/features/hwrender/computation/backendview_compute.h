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

// [rc4l] The column-major view-projection for a camera: heights stretched by the pixel aspect before the pitch turns them, as GL does, so every view keeps GL's framing.
void ComputeBackendMVP(const BackendCamera &c, float out[16]);

}} // namespace zx::hwrender

#endif // ZX_BACKENDVIEW_COMPUTE_H
