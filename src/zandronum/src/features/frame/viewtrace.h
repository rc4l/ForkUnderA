// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_viewtrace: the angle each frame's main view was drawn with, recorded for a number of frames and judged.

#ifndef ZX_VIEWTRACE_H
#define ZX_VIEWTRACE_H

namespace zx { namespace frame {

// [rc4l] The main view as set up for this frame, recorded while a trace is armed; once a frame however many renderers set it up.
void NoteMainView();

// [rc4l] Record the next `frames` frames' main view and report them once they are in, every frame written to `csv` when it is named.
void ArmViewTrace(int frames, const char *csv);

// [rc4l] Report an armed trace now, with however many frames it has.
void EndViewTrace();

// [rc4l] A raw mouse message the window received, `taken` when the engine used it (win32/i_mouse.cpp).
void NoteRawMouse(int dx, int dy, bool taken);

// [rc4l] A mouse event posted to the game, `toView` when it turned the view (D_PostEvent).
void NoteMouseEvent(bool toView);

// [rc4l] The window grabbed or let go of the mouse.
void NoteMouseGrabbed(bool grabbed);

// [rc4l] The game became the active app or stopped being it.
void NoteAppActive(bool active);

}} // namespace zx::frame

#endif // ZX_VIEWTRACE_H
