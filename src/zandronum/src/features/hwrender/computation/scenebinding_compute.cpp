// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/scenebinding_compute.h"

namespace zx { namespace hwrender {

int ComputeBatchPipeline(int blendMode)
{
	if (blendMode == 0) return kBatchPipelineMasked;
	return (blendMode == 2) ? kBatchPipelineAdd : kBatchPipelineTrans;
}

bool ComputeSceneTargetsRebuild(bool haveTargets, int targetW, int targetH, int swapW, int swapH)
{
	return !haveTargets || targetW != swapW || targetH != swapH;
}

}} // namespace zx::hwrender
