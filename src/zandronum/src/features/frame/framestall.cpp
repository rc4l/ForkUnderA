// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/framestall.h"
#include "features/frame/computation/framestall_compute.h"

#include "c_cvars.h"

#include <chrono>

// [rc4l] Microseconds burned on each frame that ran a game tic, as a heavy map's tic frame costs (debug).
CVAR(Int, fua_debug_ticstall_us, 0, 0)
// [rc4l] Microseconds burned on every frame, the same cost spread evenly as the control (debug).
CVAR(Int, fua_debug_framestall_us, 0, 0)

namespace zx { namespace frame {

void DebugFrameStall(bool ticRan)
{
	const int us = ComputeFrameStallUs(ticRan, fua_debug_ticstall_us, fua_debug_framestall_us);
	if (us <= 0) return;
	const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
	while (std::chrono::steady_clock::now() < until) {}
}

}} // namespace zx::frame
