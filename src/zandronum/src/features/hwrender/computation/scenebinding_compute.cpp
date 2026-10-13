// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/scenebinding_compute.h"

namespace zx { namespace hwrender {

bool ComputeSceneTargetsRebuild(bool haveTargets, int targetW, int targetH, int swapW, int swapH)
{
	return !haveTargets || targetW != swapW || targetH != swapH;
}

LoopCommits ComputeLoopCommitsStart()
{
	LoopCommits loop;
	loop.count = 0;
	for (int i = 0; i < kLoopCommitsMax; i++) loop.seen[i] = 0;
	return loop;
}

bool ComputeCommitTransitions(LoopCommits &loop, const void *binding)
{
	for (int i = 0; i < loop.count; i++)
		if (loop.seen[i] == binding) return false;
	if (loop.count < kLoopCommitsMax) loop.seen[loop.count++] = binding;
	return true;
}

}} // namespace zx::hwrender
