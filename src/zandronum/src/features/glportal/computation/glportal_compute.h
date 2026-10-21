// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's portal-ordering decisions as engine-free numbers, so a recursion they allow can be tested.

#ifndef ZX_GLPORTAL_COMPUTE_H
#define ZX_GLPORTAL_COMPUTE_H

namespace zx { namespace glportal {

// [rc4l] Whether RenderFirstSkyPortal may draw a portal before the scene: one needing the depth buffer only in the player's own scene, which is portal depth 1 and DrawScene recursion 0.
bool ComputeDrawsSkyPortalFirst(bool needsDepthBuffer, int sceneRecursion, int portalDepth);

}} // namespace zx::glportal

#endif
