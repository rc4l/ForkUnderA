// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which lights a wall applies THIS frame -- and never which ones it applied in an earlier one.
//
// A wall tells the shader where its lights are by an index into a persistently mapped buffer. Stock
// GL rebuilds every wall each frame, so the index is written twice per frame and can never be older
// than the frame drawing it: PutWall clears it, and the lit pass fills it in.
//
// The wall CACHE breaks that. A replayed wall is the same GLWall object frame after frame, so its
// index is whatever was last written into it -- and the pass that writes it only runs while at
// least one dynamic light is alive (gl_scene.cpp picks GLPASS_PLAIN over GLPASS_ALL when
// mLightCount is zero). Fire a plasma rifle and stop: the lights die, the pass stops running, the
// buffer still holds the last frame's light data, and every replayed wall keeps pointing at it. A
// dead plasma bolt goes on lighting the wall it hit, in a blue pool with the scorch in the middle,
// until something else spawns a light and the pass runs again -- which is why firing any other
// weapon clears it, and why it was reported as "the light lingers right as it is about to die".
//
// Measured on dbab04 as +1.55 of blue on wall the marks do not cover, in GL only, still there 400
// tics later, gone the moment a shotgun fired.
//
// So the index is a per-frame answer, and this is the rule that says so.

#ifndef ZX_WALLLIGHT_COMPUTE_H
#define ZX_WALLLIGHT_COMPUTE_H

namespace zx { namespace hwrender {

// What the render state means by "this surface has no dynamic lights". GLWall::dynlightindex is an
// int and the light buffer returns -1 for an empty upload; UINT_MAX assigned to it is the same
// value, which is what the per-frame path has always written.
extern const int kNoWallLightIndex;

// The index a wall should hand the shader.
//
//   lightPassRan          did the pass that collects this frame's lights run for this wall
//   computedThisFrame     what that pass produced (-1 when the wall received no light)
//   carriedOver           what the wall was already holding -- a captured or previous-frame index
//
// carriedOver is an argument only so that the rule can say what to do with it: nothing. It is never
// the answer, no matter how valid it looks, because a light index outlives the light it points at.
int ComputeWallLightIndex(bool lightPassRan, int computedThisFrame, int carriedOver);

}} // namespace zx::hwrender

#endif // ZX_WALLLIGHT_COMPUTE_H
