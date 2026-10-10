// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/slotremap_compute.h"

namespace zx { namespace hwrender {

std::vector<int> ComputeAnimationOfTexture(int numTextures, const std::vector<AnimationDef> &anims)
{
	std::vector<int> of(numTextures > 0 ? (size_t)numTextures : 0, -1);
	for (int a = 0; a < (int)anims.size(); a++)
	{
		const AnimationDef &d = anims[a];
		// [rc4l] UpdateAnimations translates only a discrete animation's base, but every member of a range.
		const int first = d.basePic, last = d.discrete ? d.basePic : d.basePic + d.numFrames - 1;
		for (int id = first; id <= last; id++)
			if (id >= 0 && id < numTextures && of[id] < 0) of[id] = a;
	}
	return of;
}

std::vector<int> ComputeAnimationFrames(const AnimationDef &anim)
{
	if (anim.discrete) return anim.pics;
	std::vector<int> frames;
	for (int i = 0; i < anim.numFrames; i++) frames.push_back(anim.basePic + i);
	return frames;
}

}} // namespace zx::hwrender
