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

// [rc4l] Every light's ids sorted, deduplicated and appended to ids, with its range; a light whose ids would pass capacity touches everything.
void ComputeLightTouchLists(const std::vector<std::vector<unsigned> > &perLight, unsigned capacity,
	std::vector<LightTouchRange> &ranges, std::vector<unsigned> &ids);

// [rc4l] Whether a light's range holds id, by the binary search the scene shader runs.
bool ComputeLightTouches(const std::vector<unsigned> &ids, const LightTouchRange &r, unsigned id);

}} // namespace zx::hwrender

#endif // ZX_LIGHTTOUCH_COMPUTE_H
