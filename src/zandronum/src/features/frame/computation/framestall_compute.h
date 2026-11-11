// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A debug stall for proving what makes frames judder: extra CPU time burned on every frame or inside every tic.

#ifndef ZX_FRAMESTALL_COMPUTE_H
#define ZX_FRAMESTALL_COMPUTE_H

namespace zx { namespace frame {

// [rc4l] The microseconds a debug stall burns for what was asked, a negative counting as none.
int ComputeStallUs(int askedUs);

}} // namespace zx::frame

#endif // ZX_FRAMESTALL_COMPUTE_H
