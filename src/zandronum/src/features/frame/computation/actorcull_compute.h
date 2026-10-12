// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The standalone sprite sweep's frustum test: whether an actor can be on screen, before anything is derived for it.

#ifndef ZX_ACTORCULL_COMPUTE_H
#define ZX_ACTORCULL_COMPUTE_H

namespace zx { namespace frame {

enum { kActorInView = 0, kActorBehind = 1, kActorOffScreen = 2 };

// [rc4l] An actor's middle (x, up, y) through the column-major `mvp`, padded by `pad` map units for a sprite wider or taller than the actor: behind the eye, off the screen's sides, or possibly in view.
int ComputeActorCull(const float *mvp, float px, float py, float pz, float pad);

}} // namespace zx::frame

#endif // ZX_ACTORCULL_COMPUTE_H
