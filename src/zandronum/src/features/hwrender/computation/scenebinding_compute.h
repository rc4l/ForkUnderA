// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the Vulkan scene binds and rebuilds: which pipeline a batch draws with, and when the swapchain's size rebuilds the depth targets.

#ifndef ZX_SCENEBINDING_COMPUTE_H
#define ZX_SCENEBINDING_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] The scene pipelines a batch can draw with.
enum { kBatchPipelineMasked = 0, kBatchPipelineAdd = 1, kBatchPipelineTrans = 2 };

// [rc4l] A batch's pipeline from its blend mode: 0 masked (opaque), 2 additive, anything else translucent.
int ComputeBatchPipeline(int blendMode);

// [rc4l] Whether the depth and normal targets are rebuilt for the swapchain: once, then for every new size, a minimise's 1x1 included.
bool ComputeSceneTargetsRebuild(bool haveTargets, int targetW, int targetH, int swapW, int swapH);

}} // namespace zx::hwrender

#endif // ZX_SCENEBINDING_COMPUTE_H
