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

} // namespace zx
