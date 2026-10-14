// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's light links for the shader: a dynamic light lights only the subsectors and sidedefs its flood through two-sided segs reached (ADynamicLight::CollectWithinRadius), never the room behind a solid wall.

#ifndef ZX_LIGHTTOUCH_COMPUTE_H
#define ZX_LIGHTTOUCH_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] One light's run of sorted ids in the flattened list.
struct LightTouchRange
{
	unsigned offset, count;
};

// [rc4l] The count that means the light was not linked here (switched off, or its ids did not fit) and so lights everything, as it did before linking.
static const unsigned kLightTouchesAll = 0xFFFFFFFFu;

// [rc4l] One light's ids as the shader searches them: sorted, each once.
void ComputeSortedTouches(std::vector<unsigned> &touched);

// [rc4l] A dynamic light's colour as gl_GetLight scales it: each 0..255 channel times r_lights_intensity, an additive light's a fifth of that.
void ComputeDynLightColor(int r, int g, int b, bool additive, float intensity, float out[3]);

// [rc4l] The tic, level and light switches the lights were last collected under.
struct LightsStamp
{
	int tic, generation, flags;
};

// [rc4l] Whether the lights must be collected again, once per tic or on a new level or switch setting; `newLevel` says the kept links belong to another level.
bool ComputeLightsDue(LightsStamp &kept, int tic, int generation, int flags, bool &newLevel);

// [rc4l] What a light's links were collected for: the light, where it stood and how far it reached.
struct LightLinkKey
{
	const void *light;
	long long x, y, z;
	float radius;
};

// [rc4l] Whether a light's kept links must be collected again, ADynamicLight relinking only when one of these changes.
bool ComputeLightLinksStale(const LightLinkKey &kept, const LightLinkKey &now);

// [rc4l] Every light's sorted ids appended to ids, with its range; a light whose ids would pass capacity touches everything.
void ComputeLightTouchLists(const std::vector<const std::vector<unsigned> *> &perLight, unsigned capacity,
	std::vector<LightTouchRange> &ranges, std::vector<unsigned> &ids);

// [rc4l] Whether a light's range holds id, by the binary search the scene shader runs.
bool ComputeLightTouches(const std::vector<unsigned> &ids, const LightTouchRange &r, unsigned id);

}} // namespace zx::hwrender

#endif // ZX_LIGHTTOUCH_COMPUTE_H
