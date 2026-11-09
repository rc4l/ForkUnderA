// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/mouse-capture/computation/nativemouse_compute.h"

namespace zx {

bool ComputeWantNativeMouse(const NativeMouseInputs &in)
{
	if (in.inputLocked)
		return true;

	// [Doruk-Aksoy] Fullscreen used to skip this, so alt-tab left the raw grab on and the pointer could not reach another monitor.
	if (!in.foreground || in.preferNative)
		return true;

	if (in.fullscreen)
		return in.menuMouse && in.menuOpen;

	if (!in.useMouse)
		return true;
	if (in.menuWaitKey)
		return false;
	return !in.captureInGame || in.guiCapture || in.paused || in.demoPlayback;
}

} // namespace zx
