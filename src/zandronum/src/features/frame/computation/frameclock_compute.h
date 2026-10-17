// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The clock a frame's warps, animated textures and sky scroll read, which a capture can pin so its picture does not depend on when it was taken.

#ifndef ZX_FRAMECLOCK_COMPUTE_H
#define ZX_FRAMECLOCK_COMPUTE_H

namespace zx { namespace frame {

// [rc4l] The milliseconds a frame reads: `pinnedMs` when it is above zero, else the real clock `nowMs`.
unsigned int ComputeFrameClock(int pinnedMs, unsigned int nowMs);

}} // namespace zx::frame

#endif // ZX_FRAMECLOCK_COMPUTE_H
