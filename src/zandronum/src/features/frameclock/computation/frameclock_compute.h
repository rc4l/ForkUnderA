// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The frame's clock: where in its tic a frame is, to the microsecond.

#ifndef ZX_FRAMECLOCK_COMPUTE_H
#define ZX_FRAMECLOCK_COMPUTE_H

namespace zx { namespace frameclock {

// [rc4l] A performance counter reading in microseconds, as std::chrono::steady_clock counts it, without overflowing on a long uptime.
long long ComputeCounterMicros(long long counter, long long frequency);

// [rc4l] Microseconds from millisecond `startMs` of a wrapping 32-bit millisecond clock to `nowUs` on the microsecond clock it counts.
long long ComputeSinceMsUs(long long nowUs, unsigned startMs);

// [rc4l] How far through a tic `lengthUs` long a frame `sinceUs` into it is, in 16.16 fixed point from 0 to 1; 1 for an empty tic.
int ComputeTicFrac(long long sinceUs, long long lengthUs);

// [rc4l] `us` cut to whole milliseconds, toward zero, as a millisecond clock would have counted it.
long long ComputeWholeMsUs(long long us);

}} // namespace zx::frameclock

#endif // ZX_FRAMECLOCK_COMPUTE_H
