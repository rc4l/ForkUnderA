// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Whether Windows keeps its own pointer or the game grabs the mouse, for the Win32 input backend.
#ifndef ZX_NATIVEMOUSE_COMPUTE_H
#define ZX_NATIVEMOUSE_COMPUTE_H

namespace zx {

struct NativeMouseInputs
{
	bool fullscreen;
	bool foreground;      // [Doruk-Aksoy] Our window is the one Windows has in front.
	bool preferNative;    // [Doruk-Aksoy] The caller asks for the pointer back now, as losing focus does.
	bool useMouse;        // [Doruk-Aksoy] use_mouse: the mouse aims in game at all.
	bool menuMouse;       // [Doruk-Aksoy] m_use_mouse: menus take the pointer.
	bool menuOpen;        // [Doruk-Aksoy] A menu is up (MENU_On or MENU_OnNoPause).
	bool menuWaitKey;     // [Doruk-Aksoy] A binding is waiting for its key, which may be a mouse button.
	bool captureInGame;   // [Doruk-Aksoy] CaptureMode_InGame: in a level where the mouse aims.
	bool guiCapture;
	bool paused;
	bool demoPlayback;
	bool inputLocked;     // [Doruk-Aksoy] A hands-off fuactl instance, which never takes the user's pointer.

	NativeMouseInputs()
		: fullscreen(false), foreground(true), preferNative(false), useMouse(true), menuMouse(true), menuOpen(false),
		  menuWaitKey(false), captureInGame(true), guiCapture(false), paused(false), demoPlayback(false), inputLocked(false) {}
};

// [Doruk-Aksoy] True when the pointer should be Windows', which it always is once our window is not in front, fullscreen or not.
bool ComputeWantNativeMouse(const NativeMouseInputs &in);

} // namespace zx

#endif // ZX_NATIVEMOUSE_COMPUTE_H
