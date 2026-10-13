// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How far the CPU may run ahead of the GPU: each frame signals a fence with its number, and a frame more than `maxAhead` ahead waits for the one that many back.

#ifndef ZX_FRAMEPACING_COMPUTE_H
#define ZX_FRAMEPACING_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The fence value to wait for once frame `signalled` is queued, 0 for none (the first frames, or pacing off with `maxAhead` 0).
unsigned long long ComputeFenceWaitValue(unsigned long long signalled, unsigned long long maxAhead);

}} // namespace zx::hwrender

#endif // ZX_FRAMEPACING_COMPUTE_H
