// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/dynlights/pointlight.h"
#include "features/gamecopy/actors.h"   // [rc4l] ActorRecord
#include "features/gamecopy/lights.h"   // [rc4l] the light records and each subsector's light list
#include "features/hwrender/computation/lighttouch_compute.h"   // [rc4l] ComputeDynLightColor
#include "features/sprites/computation/spritelight_compute.h"   // [rc4l] ComputeSpriteLightTerm

#include "r_defs.h"
#include "r_state.h"
#include "vectors.h"   // [rc4l] FVector3
#include "c_cvars.h"

EXTERN_CVAR(Float, r_lights_size)
EXTERN_CVAR(Float, r_lights_intensity)

namespace zx { namespace dynlights {

// [rc4l] gl_SetDynSpriteLight: the dynamic light at a point in a subsector, from the render copy's lights linked there in its light list's order, but not from a light `self` carries that does not light it.
void PointLight(fixed_t x, fixed_t y, fixed_t z, subsector_t *subsec, const void *self, float out[3])
{
	int count, all;
	const int *linked = zx::gamecopy::SubsectorLights(int(subsec - subsectors), count);
	const zx::gamecopy::LightRecord *const lights = zx::gamecopy::Lights(all);
	for (int i = 0; i < count; i++)
	{
		const zx::gamecopy::LightRecord &light = lights[linked[i]];
		if (!light.active || (light.dontLightSelf && light.target == self)) continue;
		const float dist = FVector3(FIXED2FLOAT(x - light.x), FIXED2FLOAT(y - light.y), FIXED2FLOAT(z - light.z)).Length();
		const float radius = light.radius * r_lights_size;
		// [rc4l] gl_SetDynSpriteLight takes the light's colour without the additive scaling a surface's light has.
		float c[3];
		zx::hwrender::ComputeDynLightColor(light.red, light.green, light.blue, false, r_lights_intensity, c);
		const float bright = light.subtractive ? (float)FVector3(c[0], c[1], c[2]).Length() : 0.f;
		zx::sprites::ComputeSpriteLightTerm(dist, radius, c[0], c[1], c[2], light.subtractive, bright, out);
	}
}

// [rc4l] gl_SetDynSpriteLight for an actor, summed the way GL sums it.
void ActorLight(const zx::gamecopy::ActorRecord &self, float out[3])
{
	PointLight(self.x, self.y, self.z + (self.height >> 1), self.subsector, self.who, out);
}

void PointDynamicLight(fixed_t x, fixed_t y, fixed_t z, subsector_t *subsec, float out[3])
{
	out[0] = out[1] = out[2] = 0.f;
	if (subsec != NULL) PointLight(x, y, z, subsec, NULL, out);
}

}} // namespace zx::dynlights
