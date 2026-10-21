// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] vid_maxfps and cl_capfps held as a frame rate: a wait before each present on the desktop, and a browser's animation frames skipped until one is due.

#ifndef ZX_FRAMELIMIT_H
#define ZX_FRAMELIMIT_H

namespace zx { namespace framelimit {

// [rc4l] The frames a second the limit holds the frame rate to now, 0 for none.
int LimitFps();

// [rc4l] Waits until this frame may be shown, called just before every present; a stepped loop never waits here.
void WaitBeforePresent();

// [rc4l] Whether a stepped loop's frame may be drawn now, called at the top of each one; false skips the frame.
bool SteppedFrameDue();

}} // namespace zx::framelimit

#endif // ZX_FRAMELIMIT_H
