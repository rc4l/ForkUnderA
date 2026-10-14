// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/coverage_compute.h"

namespace zx { namespace visibility {

static bool Child(const CoverageNode *nodes, int child, const unsigned char *subCovered, unsigned char *nodeCovered)
{
	return child < 0 ? subCovered[~child] != 0 : ComputeNodeCoverage(nodes, child, subCovered, nodeCovered);
}

bool ComputeNodeCoverage(const CoverageNode *nodes, int node, const unsigned char *subCovered, unsigned char *nodeCovered)
{
	// [rc4l] Both sides always, as GL's | does, so every node below is marked.
	const bool front = Child(nodes, nodes[node].child[0], subCovered, nodeCovered);
	const bool back = Child(nodes, nodes[node].child[1], subCovered, nodeCovered);
	nodeCovered[node] = (front || back) ? 1 : 0;
	return front || back;
}

}} // namespace zx::visibility
