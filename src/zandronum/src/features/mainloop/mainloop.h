// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How the main loop is driven: the desktop's endless loop, or a browser calling one frame at a time, where nothing may wait.

#ifndef ZX_MAINLOOP_H
#define ZX_MAINLOOP_H

namespace zx { namespace mainloop {

// [rc4l] A driver that calls `frame` once a frame from outside and returns at once, as a browser's main loop does.
typedef void (*FrameDriver)(void (*frame)());

// [rc4l] Whether the loop is stepped by a driver, so a wipe advances once a frame instead of running to its end where it starts.
bool Stepped();
// [rc4l] Install the driver, before D_DoomMain; NULL is the desktop's own loop.
void SetDriver(FrameDriver driver);

// [rc4l] Thrown by StartStepped to unwind D_DoomMain once the driver has the loop, caught where the driver called it from.
struct SteppedLoopStarted {};
// [rc4l] Hand `frame` to the driver and unwind to its caller with SteppedLoopStarted.
void StartStepped(void (*frame)());

// [rc4l] Quit after `vbls` refreshes of Doom's 70 Hz, a quit sound's time: waiting and ending here on the desktop, and on a stepped loop at the first frame past it.
void Quit(int vbls);
// [rc4l] Ends the program once a stepped loop's quit has had its time, called at the top of every frame.
void FinishPendingQuit();
// [rc4l] Whether a stepped loop's quit is waiting out its time.
bool QuitPending();

}} // namespace zx::mainloop

#endif // ZX_MAINLOOP_H
