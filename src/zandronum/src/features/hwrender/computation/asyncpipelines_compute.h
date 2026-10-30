// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Whether the backend's pipelines are made in the background, which needs a device that can wait for them.

#ifndef ZX_ASYNCPIPELINES_COMPUTE_H
#define ZX_ASYNCPIPELINES_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] Background pipelines as wanted, only where the CPU can wait for the GPU: a browser's WebGPU cannot, so a wait there answers "still compiling" and the level's pipelines read as failed.
bool ComputeAsyncPipelines(bool wanted, bool canWaitForGPU);

}} // namespace zx::hwrender

#endif // ZX_ASYNCPIPELINES_COMPUTE_H
