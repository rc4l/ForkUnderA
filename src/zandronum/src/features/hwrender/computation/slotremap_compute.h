// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] ANIMDEFS frames for the bindless material array: which slot a baked piece's texture shows this frame, without rewriting a descriptor the GPU may still be reading.

#ifndef ZX_SLOTREMAP_COMPUTE_H
#define ZX_SLOTREMAP_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] One ANIMDEFS animation by texture index: a range runs numFrames ids from basePic, a discrete one shows the ids in pics.
struct AnimationDef
{
	int basePic;
	int numFrames;
	bool discrete;
	std::vector<int> pics;
};

// [rc4l] For each texture index below numTextures, the animation that can change what it shows (every member of a range, a discrete animation's base), or -1.
std::vector<int> ComputeAnimationOfTexture(int numTextures, const std::vector<AnimationDef> &anims);

// [rc4l] Every texture index the animation can show.
std::vector<int> ComputeAnimationFrames(const AnimationDef &anim);

// [rc4l] remap[i] becomes the slot showing slot i's texture now, where shows(i) names it or is negative to keep i; new entries start as themselves, slot 0 is never asked, and true means any entry changed.
template <typename Shows>
bool ComputeSlotRemap(unsigned count, Shows shows, std::vector<unsigned> &remap)
{
	bool changed = remap.size() < count;
	for (unsigned i = (unsigned)remap.size(); i < count; i++) remap.push_back(i);
	for (unsigned i = 1; i < count; i++)
	{
		const int s = shows(i);
		const unsigned want = (s >= 0 && (unsigned)s < count) ? (unsigned)s : i;
		if (remap[i] != want) { remap[i] = want; changed = true; }
	}
	return changed;
}

// [rc4l] Whether the bindless material array keeps its last fill: built, filled from this same table, and with no live material that came back as the white placeholder, which a later fill may resolve (a sprite not yet uploaded, a camera not yet rendered).
bool ComputeKeepsMaterialFill(bool ready, bool tableMatches, int whiteLiveSlots);

}} // namespace zx::hwrender

#endif // ZX_SLOTREMAP_COMPUTE_H
