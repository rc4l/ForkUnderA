// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLSectorStackPortal::SetupCoverage's node pass: which BSP nodes hold a subsector the portal covers, so a stacked view's walk enters them whatever its clipper says.

#ifndef ZX_COVERAGE_COMPUTE_H
#define ZX_COVERAGE_COMPUTE_H

namespace zx { namespace visibility {

// [rc4l] A node's two children as indices: a node's own, or ~subsector for a leaf.
struct CoverageNode { int child[2]; };

// [rc4l] Mark every node under `node` that holds a covered subsector in nodeCovered, as GL's SetCoverage does, and say whether `node` does.
bool ComputeNodeCoverage(const CoverageNode *nodes, int node, const unsigned char *subCovered, unsigned char *nodeCovered);

}} // namespace zx::visibility

#endif // ZX_COVERAGE_COMPUTE_H
