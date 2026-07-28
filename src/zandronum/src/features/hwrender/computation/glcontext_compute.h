// [rc4l] Compute the ordered list of GL context versions/profiles to request at window creation.
//
// A GL context is core or compatibility for its whole life, so the profile has to be chosen up front
// from vid_hwrender (0 = legacy renderer on a compatibility context, 1 = ported backend on a core
// context). SDL2 can finally express this via SDL_GL_SetAttribute(CONTEXT_MAJOR/MINOR/PROFILE_MASK);
// SDL 1.2 could not, which is why macOS was stuck at 2.1. We try the highest version first and fall
// back, because Apple caps core at 4.1 and old drivers cap compat at 2.1.
//
// Pure so the fallback chain is unit-tested; the SDL glue just walks the list calling SDL_CreateWindow.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_GLCONTEXT_COMPUTE_H
#define ZX_GLCONTEXT_COMPUTE_H

#include <cstddef>

namespace zx
{

struct GLContextRequest
{
	int major;
	int minor;
	bool coreProfile; // true => core (forward-compatible), false => compatibility
};

// [rc4l] Max attempts either chain can produce; lets the glue use a fixed C array with no allocation.
constexpr int kMaxGLContextRequests = 4;

// [rc4l] Fill `out` (capacity kMaxGLContextRequests) with the requests to try in order and return
// the count. wantCore selects the core chain (4.1 -> 4.0 -> 3.3) or the compatibility chain
// (3.0 -> 2.1). Returns 0 and writes nothing if out is null or capacity is too small.
int ComputeGLContextRequests(bool wantCore, GLContextRequest *out, int capacity);

} // namespace zx

#endif // ZX_GLCONTEXT_COMPUTE_H
