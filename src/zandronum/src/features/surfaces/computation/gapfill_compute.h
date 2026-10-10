// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What GL draws for a step whose upper or lower texture is missing: GLWall::Process's fallbacks and FDrawInfo's missing-texture lists, decided from the map.

#ifndef ZX_GAPFILL_COMPUTE_H
#define ZX_GAPFILL_COMPUTE_H

namespace zx { namespace surfaces {

// [rc4l] Nothing (GL leaves a hole), the front sector's flat as a wall (a sloped step), or the back plane flooded into the gap.
enum { kGapNone = 0, kGapFrontFlat = 1, kGapFlood = 2 };

struct GapInputs
{
	bool top;          // [rc4l] the upper tier, a ceiling step, rather than the lower
	bool sloped;       // [rc4l] either sector's plane on this side is sloped
	bool frontSky, backSky;
	bool backPortal;   // [rc4l] the back plane is a portal: a skybox or a stacked sector
	bool transdoor;    // [rc4l] a transparent-door sector, which fakes its own floor height
	bool polyobj;
	bool fixAbuse;     // [rc4l] the back sector's first 3D floor is FF_FIX without FF_SEETHROUGH, which uses a missing lower texture on purpose
	bool fakeFlat;     // [rc4l] the front sector renders through another's planes (Transfer_Heights)
	bool hasPartner;   // [rc4l] the seg has a partner on the other side, which GL needs to flood from
};

int ComputeGapFill(const GapInputs &in);

}} // namespace zx::surfaces

#endif // ZX_GAPFILL_COMPUTE_H
