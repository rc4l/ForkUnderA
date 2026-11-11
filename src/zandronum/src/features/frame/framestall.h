// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_debug_framestall_us and fua_debug_playsim_us: CPU time burned per frame or per tic, to test what makes frames judder.

#ifndef ZX_FRAMESTALL_H
#define ZX_FRAMESTALL_H

namespace zx { namespace frame {

// [rc4l] Burn fua_debug_framestall_us, once the loop's tics for this frame have run.
void DebugFrameStall();

// [rc4l] Burn fua_debug_playsim_us inside a tic's playsim, on whichever thread runs it, as a heavy fight's tic costs.
void DebugPlaysimStall();

}} // namespace zx::frame

#endif // ZX_FRAMESTALL_H
