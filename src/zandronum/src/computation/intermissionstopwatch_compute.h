// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How a client counts down the intermission's time left, which the server ends on its own.

#ifndef ZX_INTERMISSIONSTOPWATCH_COMPUTE_H
#define ZX_INTERMISSIONSTOPWATCH_COMPUTE_H

namespace zx {

// [rc4l] Ported from zandronum@1d9962b2229fe9ef256e871ba2056e3a18bfce61: a tic's count down, held while a clientside demo's playback is paused and at zero.
long ComputeClientStopWatch(long stopWatch, bool demoPaused);

} // namespace zx

#endif
