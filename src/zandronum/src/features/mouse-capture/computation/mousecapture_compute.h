// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the game does with the mouse this tic: a desktop's SDL holds it whenever the game asks, while a page's pointer lock comes and goes on its own (Escape drops it), and without it SDL reports where the cursor is rather than how it moved.

#ifndef ZX_MOUSECAPTURE_COMPUTE_H
#define ZX_MOUSECAPTURE_COMPUTE_H

namespace zx {

struct MouseCapture
{
	// [rc4l] Relative motion is aiming.
	bool readMotion;
	// [rc4l] Buttons reach the game; the click that asks for the pointer back is not a shot.
	bool takeClicks;
	// [rc4l] The cursor shows, so the player sees where to click.
	bool showCursor;
};

// [rc4l] From whether the game wants the mouse (in play, not in a menu) and whether the window holds it now.
MouseCapture ComputeMouseCapture(bool wantMouse, bool held);

} // namespace zx

#endif // ZX_MOUSECAPTURE_COMPUTE_H
