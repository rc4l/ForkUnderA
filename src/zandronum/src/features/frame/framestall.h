// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_debug_ticstall_us and fua_debug_framestall_us: CPU time burned per frame, to test what makes frames judder.

#ifndef ZX_FRAMESTALL_H
#define ZX_FRAMESTALL_H

namespace zx { namespace frame {

// [rc4l] Burn the debug stall due this frame, once the loop's tics for it have run.
void DebugFrameStall(bool ticRan);

}} // namespace zx::frame

#endif // ZX_FRAMESTALL_H
