// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A debug stall for proving what makes frames judder: extra time burned on the frames that ran a game tic, on every frame, or both.

#ifndef ZX_FRAMESTALL_COMPUTE_H
#define ZX_FRAMESTALL_COMPUTE_H

namespace zx { namespace frame {

// [rc4l] The microseconds to burn this frame: `ticStallUs` when a tic ran in it, plus `everyFrameUs` always, negatives counting as none.
int ComputeFrameStallUs(bool ticRan, int ticStallUs, int everyFrameUs);

}} // namespace zx::frame

#endif // ZX_FRAMESTALL_COMPUTE_H
