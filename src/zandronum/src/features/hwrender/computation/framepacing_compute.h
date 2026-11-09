// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How far the CPU may run ahead of the GPU: each frame signals a fence with its number, and a frame more than `maxAhead` ahead waits for the one that many back.

#ifndef ZX_FRAMEPACING_COMPUTE_H
#define ZX_FRAMEPACING_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The fence value to wait for once frame `signalled` is queued, 0 for none (the first frames, or pacing off with `maxAhead` 0 or below).
unsigned long long ComputeFenceWaitValue(unsigned long long signalled, int maxAhead);

// [rc4l] The swapchain's present interval: 1 with vsync on, or on a page's canvas, which the browser presents at its compositor's pace whatever is asked and whose swapchain is remade each time the interval changes; else 0.
int ComputePresentInterval(bool vsync, bool pagePaced);

// [Doruk-Aksoy] The swapchain images to ask for: `forced` when above 0, else two with vsync on, so the CPU cannot queue a second frame ahead and fall into a 1/6/14 ms rhythm at 144 Hz, and three for mailbox; a page's canvas, which is never remade, always three.
int ComputeSwapchainImages(bool vsync, bool pagePaced, int forced = 0);

}} // namespace zx::hwrender

#endif // ZX_FRAMEPACING_COMPUTE_H
