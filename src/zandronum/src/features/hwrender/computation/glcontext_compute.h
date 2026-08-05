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

// [rc4l] The Cocoa side of the same decision. Apple's NSOpenGLPFAOpenGLProfile takes exactly three
// values -- there is no 4.0 or 3.3 constant -- so the chain above collapses onto them and must be
// de-duplicated, or we would ask the OS for the identical pixel format twice. Kept here rather than
// in i_video.mm so the collapse is testable without AppKit; the values are Apple's, mirrored so
// this header stays engine-free and C++14.
enum
{
	kNSGLProfileLegacy = 0x1000, // NSOpenGLProfileVersionLegacy   -> GL 2.1
	kNSGLProfileCore32 = 0x3200, // NSOpenGLProfileVersion3_2Core  -> GL 3.2+
	kNSGLProfileCore41 = 0x4100  // NSOpenGLProfileVersion4_1Core  -> GL 4.1, 10.10+
};

// [rc4l] Widest Apple profile that can satisfy `req`. A non-core request is always Legacy.
constexpr int kMaxCocoaGLProfiles = 3;
int ComputeCocoaGLProfile(const GLContextRequest &req);

// [rc4l] Maps ComputeGLContextRequests through ComputeCocoaGLProfile, drops consecutive duplicates
// and appends Legacy as the last resort. Returns the count written to `out` (capacity
// kMaxCocoaGLProfiles); 0 and nothing written if out is null or capacity too small.
int ComputeCocoaGLProfileChain(bool wantCore, int *out, int capacity);

} // namespace zx

#endif // ZX_GLCONTEXT_COMPUTE_H
