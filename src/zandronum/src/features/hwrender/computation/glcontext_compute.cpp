// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/hwrender/computation/glcontext_compute.h"

namespace zx
{

int ComputeGLContextRequests(bool wantCore, GLContextRequest *out, int capacity)
{
	if (out == nullptr || capacity < kMaxGLContextRequests)
	{
		return 0;
	}

	if (wantCore)
	{
		// [rc4l] Core chain. 4.1 is Apple's ceiling; 3.3 is the floor the ported shaders (#version
		// 330 core) require.
		out[0] = GLContextRequest{4, 1, true};
		out[1] = GLContextRequest{4, 0, true};
		out[2] = GLContextRequest{3, 3, true};
		return 3;
	}

	// [rc4l] Compatibility chain for the legacy renderer: 3.0 first so we still get VAOs/where
	// available, falling back to the 2.1 that the fixed-function + GLSL 1.20 path minimally needs.
	out[0] = GLContextRequest{3, 0, false};
	out[1] = GLContextRequest{2, 1, false};
	return 2;
}

int ComputeCocoaGLProfile(const GLContextRequest &req)
{
	// A compatibility request has no Apple core profile to land on -- Legacy IS the compatibility
	// profile there. Only genuinely core requests map upward.
	if (!req.coreProfile)
		return kNSGLProfileLegacy;
	if (req.major > 4 || (req.major == 4 && req.minor >= 1))
		return kNSGLProfileCore41;
	if (req.major > 3 || (req.major == 3 && req.minor >= 2))
		return kNSGLProfileCore32;
	return kNSGLProfileLegacy;
}

int ComputeCocoaGLProfileChain(bool wantCore, int *out, int capacity)
{
	if (out == nullptr || capacity < kMaxCocoaGLProfiles)
		return 0;

	GLContextRequest reqs[kMaxGLContextRequests];
	const int n = ComputeGLContextRequests(wantCore, reqs, kMaxGLContextRequests);

	int count = 0;
	for (int i = 0; i < n; ++i)
	{
		const int profile = ComputeCocoaGLProfile(reqs[i]);
		// 4.0 and 3.3 both collapse onto Core32; asking the OS twice for the same pixel format
		// would just be a slower way to get the same answer.
		if (count > 0 && out[count - 1] == profile)
			continue;
		out[count++] = profile;
	}

	// No bounds check is needed inside that loop, and none is written: there are only three profile
	// values in existence, consecutive duplicates are dropped, and the guard above already requires
	// capacity >= kMaxCocoaGLProfiles. A capacity test there would be a branch no input can reach.

	// Legacy is always the last resort, even when the core chain already ended there.
	if (count > 0 && out[count - 1] == kNSGLProfileLegacy)
		return count;

	out[count++] = kNSGLProfileLegacy;
	return count;
}

} // namespace zx
