// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The precache hitlist spread over the textures a marked one turns into: every frame of its ANIMDEFS animation, and a switch's other pictures.

#ifndef ZX_ANIMMARKS_COMPUTE_H
#define ZX_ANIMMARKS_COMPUTE_H

#include <vector>

namespace zx { namespace caching {

// [rc4l] The texture indices an animation shows: base onward for a range, its listed frames for a discrete one, none past count.
std::vector<int> ComputeAnimationFrames(bool discrete, int basePic, int numFrames, const std::vector<int> &listed, int count);

// [rc4l] When any of frames is marked, every frame takes every mark any of them has, so a level marking one frame of a picture precaches all of it.
void ComputeSpreadMarks(unsigned char *hitlist, int count, const std::vector<int> &frames);

}} // namespace zx::caching

#endif // ZX_ANIMMARKS_COMPUTE_H
