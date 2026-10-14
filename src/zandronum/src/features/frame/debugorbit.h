// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_debug_orbit: the aim swept in a circle every frame through the mouse's own path, to reproduce what a player sees while turning.

#ifndef ZX_DEBUGORBIT_H
#define ZX_DEBUGORBIT_H

namespace zx { namespace frame {

// [rc4l] Move the aim this frame's share of the orbit, as mouse movement arriving now would.
void DebugOrbit();

}} // namespace zx::frame

#endif // ZX_DEBUGORBIT_H
