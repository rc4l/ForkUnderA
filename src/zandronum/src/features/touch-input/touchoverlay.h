// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The on-screen controls in play: each tic the host's pointers become the stick, the look drag and key events; each frame the controls are drawn above the HUD and below the menu, only while in play (ComputeTouchShown).

#ifndef ZX_TOUCHOVERLAY_H
#define ZX_TOUCHOVERLAY_H

namespace zx { namespace touch {

// [rc4l] Each tic on the main thread, beside MouseRead: the pointers read, the stick set, the look posted as mouse motion, the buttons posted as key events.
void Tick();

// [rc4l] Each frame on the main thread, above the HUD and below the console and menu: the controls, while shown.
void Draw();

// [rc4l] A key was pressed: in auto mode the overlay hides until the next touch.
void NoteKeyboard();

// [rc4l] Whether the overlay was shown this tic, for the page and the checks (fua_touch_status).
bool Shown();

}} // namespace zx::touch

#endif // ZX_TOUCHOVERLAY_H
