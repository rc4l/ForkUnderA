// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The dynamic light on a point or an actor as GL's gl_SetDynSpriteLight sums it, for sprites, particles and decals.

#ifndef ZX_POINTLIGHT_H
#define ZX_POINTLIGHT_H

#include "basictypes.h"   // [rc4l] fixed_t

class AActor;
struct subsector_t;

namespace zx { namespace dynlights {

// [rc4l] The dynamic light at a point in a subsector, added to out, but not from a light self carries that does not light it.
void PointLight(fixed_t x, fixed_t y, fixed_t z, subsector_t *subsec, const AActor *self, float out[3]);

// [rc4l] The dynamic light at an actor's middle, added to out.
void ActorLight(const AActor *self, float out[3]);

// [rc4l] The dynamic light on an actor, from zero.
void ActorDynamicLight(const AActor *actor, float out[3]);

// [rc4l] The dynamic light at a point, from zero, and none without a subsector.
void PointDynamicLight(fixed_t x, fixed_t y, fixed_t z, subsector_t *subsec, float out[3]);

}} // namespace zx::dynlights

#endif // ZX_POINTLIGHT_H
