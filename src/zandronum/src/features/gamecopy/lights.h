// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy's dynamic lights: each light as the renderer reads it and what it is linked to, taken between tics, since a tic moves, relinks and destroys lights while a frame draws.

#ifndef ZX_GAMECOPY_LIGHTS_H
#define ZX_GAMECOPY_LIGHTS_H

#include "m_fixed.h"

#include <vector>

namespace zx { namespace gamecopy {

// [rc4l] A dynamic light as the renderer reads it.
struct LightRecord
{
	unsigned int id;              // [rc4l] its render copy id
	const void *target;           // [rc4l] who carries it, compared and never read
	fixed_t x, y, z;
	fixed_t linkRadius;           // [rc4l] how far it is linked, which a pulse leaves alone
	float radius;                 // [rc4l] GetRadius, how far it lights before r_lights_size
	int red, green, blue;
	bool active, subtractive, additive, dontLightSelf;
	std::vector<unsigned> links;  // [rc4l] the subsectors it reached and numsubsectors plus each sidedef it lights, sorted
};

// [rc4l] Every dynamic light of the last fill, in thinker order.
const LightRecord *Lights(int &count);
// [rc4l] The lights linked to a subsector, as indices into Lights in its light list's order, NULL when none.
const int *SubsectorLights(int subsector, int &count);

// [rc4l] Takes every light and each subsector's light list, a light's links kept while it stands where it was linked; part of the fill.
void FillLights();
// [rc4l] Forgets the last level's lights.
void InitLights();

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_LIGHTS_H
