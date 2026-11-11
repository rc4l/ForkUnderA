// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/framestall.h"
#include "features/frame/computation/framestall_compute.h"

#include "c_cvars.h"

#include <chrono>

// [rc4l] Microseconds burned on every frame once its tics have run, as a heavy view's frame costs (debug).
CVAR(Int, fua_debug_framestall_us, 0, CVAR_FUA_DEV_MODE)
// [rc4l] Microseconds burned inside each tic's playsim, on whichever thread runs it, as a heavy fight's tic costs (debug).
CVAR(Int, fua_debug_playsim_us, 0, CVAR_FUA_DEV_MODE)

namespace zx { namespace frame {

namespace {

// [rc4l] Spin on this thread for the stall a debug cvar asks for.
void Burn(int askedUs)
{
	const int us = ComputeStallUs(askedUs);
	if (us <= 0) return;
	const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(us);
	while (std::chrono::steady_clock::now() < until) {}
}

} // namespace

void DebugFrameStall() { Burn(fua_debug_framestall_us); }

void DebugPlaysimStall() { Burn(fua_debug_playsim_us); }

}} // namespace zx::frame
