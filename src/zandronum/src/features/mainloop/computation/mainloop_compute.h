// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The main loop's timing decisions, apart from the clocks and waits that drive them.

#ifndef ZX_MAINLOOP_COMPUTE_H
#define ZX_MAINLOOP_COMPUTE_H

namespace zx { namespace mainloop {

// [rc4l] Wipe tics due since `start` at `now` (both I_MSTime milliseconds), at the wipe's own 40 a second.
int ComputeWipeTicsDue(unsigned start, unsigned now);

// [rc4l] How long I_WaitVBL(count) waits: `count` refreshes of Doom's 70 Hz display, in milliseconds.
unsigned ComputeVblMilliseconds(int count);

// [rc4l] Whether the millisecond clock at `now` has reached `deadline`, across a wrap of the clock.
bool ComputeDeadlinePassed(unsigned deadline, unsigned now);

}} // namespace zx::mainloop

#endif // ZX_MAINLOOP_COMPUTE_H
