// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Sprite occlusion from a handful of big walls, rather than from all of them.
//
// The standalone renderer does not walk the BSP, so it has lost the walk's other job: only visiting
// subsectors that survive angular clipping, which is what kept GL from ever seeing an actor behind a
// wall. docs/occlusion-scope.md prices what that costs and rules out putting the walk back -- the
// traversal alone costs more than the sprites it would remove.
//
// What is left has to be O(actors) rather than O(visible geometry). This is that: a 1-D depth buffer
// over ANGLE, filled from the N one-sided walls that subtend the most of the view, and then one
// lookup per actor.
//
// One-sided walls are the right occluders and the only ones used here. They are solid floor to
// ceiling by construction -- there is no other side -- which is exactly the property Doom's original
// solidseg visibility relied on, and it is why height can be ignored.
//
// Conservative in one direction only: a bucket keeps the FAR end of the nearest wall covering it, and
// an actor is hidden only when its near edge is behind that in every bucket it spans. So the buffer
// under-culls where it is wrong, never over-culls, and a mistake costs performance rather than a
// missing sprite.

#ifndef ZX_OCCLUSION_H
#define ZX_OCCLUSION_H

class AActor;

namespace zx { namespace hwrender {

// Rebuild for this frame's camera from the nLines biggest blockers in the level's static ranking.
// nLines <= 0 disables the buffer, and Hidden() then answers false for everything.
void OcclusionBuild(int nLines);

// Is this actor definitely behind the walls in the buffer? False means "not known to be hidden".
bool OcclusionHidden(const AActor *thing);

// What the last build walked, what it painted, and what it cost.
void OcclusionStats(int &linesUsed, int &limitsPainted, double &buildMs);

}} // namespace zx::hwrender

#endif // ZX_OCCLUSION_H
