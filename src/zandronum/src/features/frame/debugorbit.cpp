// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/debugorbit.h"
#include "features/frame/computation/debugorbit_compute.h"

#include "c_cvars.h"
#include "g_game.h"   // [rc4l] G_AddViewAngle, G_AddViewPitch

#include <chrono>

// [rc4l] The orbit's radius in degrees, 0 for off (debug).
CVAR(Float, fua_debug_orbit, 0.f, 0)
// [rc4l] Seconds for one turn of the orbit (debug).
CVAR(Float, fua_debug_orbit_period, 1.5f, 0)

namespace zx { namespace frame {

namespace {

bool g_on = false;
std::chrono::steady_clock::time_point g_start;
long long g_yawDone = 0, g_pitchDone = 0;

} // namespace

void DebugOrbit()
{
	if (fua_debug_orbit <= 0.f) { g_on = false; return; }
	const auto now = std::chrono::steady_clock::now();
	if (!g_on) { g_on = true; g_start = now; g_yawDone = g_pitchDone = 0; }
	double yaw, pitch;
	ComputeOrbitOffset(std::chrono::duration<double>(now - g_start).count(), fua_debug_orbit, fua_debug_orbit_period, yaw, pitch);
	const int dy = ComputeOrbitUnits(yaw, g_yawDone), dp = ComputeOrbitUnits(pitch, g_pitchDone);
	g_yawDone += dy;
	g_pitchDone += dp;
	// [rc4l] As a mouse moves the view: per frame, through LocalViewAngle and LocalViewPitch.
	if (dy != 0) G_AddViewAngle(dy, true);
	if (dp != 0) G_AddViewPitch(-dp, true);
}

}} // namespace zx::frame
