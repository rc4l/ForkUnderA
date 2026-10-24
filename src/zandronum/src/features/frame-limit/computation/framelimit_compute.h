// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The frame rate limit's decisions with no clock and no engine: which limit holds, when the next frame may be shown, and how to wait for it.

#ifndef ZX_FRAMELIMIT_COMPUTE_H
#define ZX_FRAMELIMIT_COMPUTE_H

namespace zx { namespace framelimit {

// [rc4l] The frames a second the limit holds to, 0 for none: cl_capfps's tic rate over vid_maxfps, since a frame beside a game-thread tic never waits for the tic as TryRunTics does.
int ComputeFrameLimitFps(int maxfps, bool capfps, int ticrate);

// [rc4l] When the frame after one due at previousUs may be shown, a period later, or nowUs with no previous frame, more than a period behind, or a clock that went back.
long long ComputeFrameDeadline(long long previousUs, long long nowUs, int fps);

// [rc4l] Upstream's wait for a deadline: sleep until 2 ms before it, then yield (0) and look again, since an OS sleep overshoots; -1 once it is reached or over a second away.
long long ComputeFrameLimitSleepUs(long long deadlineUs, long long nowUs);

// [rc4l] Whether a frame a browser offers at nowUs may be drawn for deadlineUs, one within toleranceUs counting, since a browser offers frames only at its refresh.
bool ComputeSteppedFrameDue(long long deadlineUs, long long nowUs, long long toleranceUs);

// [rc4l] The limit's one deadline, 0 for none, kept by whichever loop paces the frames.
struct FrameLimiter { long long deadlineUs = 0; };

// [rc4l] The deadline a desktop's present waits for at fps, advanced from the last, or 0 for no wait: with no limit, or in a browser, whose frames keep the deadline themselves (ComputeSteppedFrame).
long long ComputePresentDeadline(FrameLimiter &l, long long nowUs, int fps, bool stepped);

// [rc4l] Whether a frame a browser offers at nowUs is drawn under fps (0 for none), advancing the deadline when it is.
bool ComputeSteppedFrame(FrameLimiter &l, long long nowUs, int fps, long long toleranceUs);

}} // namespace zx::framelimit

#endif // ZX_FRAMELIMIT_COMPUTE_H
