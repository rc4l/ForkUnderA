// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a floor or ceiling decal sits, and which surface it is stuck to.
//
// Both questions are small, both are easy to get backwards, and both fail silently when you do: a
// decal on the wrong plane floats above the surface it marked, or sinks under it, and neither says
// anything. One of them shipped wrong already -- see ComputeDecalUsesTopPlane.

#ifndef ZX_FLATDECAL_COMPUTE_H
#define ZX_FLATDECAL_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] Which of a 3D floor's two planes a decal is stuck to.
//
// A 3D floor is defined by a CONTROL sector that is modelled upside down: F3DFloor::top points at
// the control sector's CEILING plane and ::bottom at its FLOOR plane (p_3dfloors.cpp). So the
// surface you stand on -- the one a downward shot marks -- is `top`, and the underside you see from
// below is `bottom`.
//
// This was implemented the other way round, with a comment confidently explaining the inversion
// backwards. Returns true when the decal belongs on `top`.
//
// `hitCeiling` is what the trace reported: false for a shot landing on a walkable surface, true for
// one hitting an underside.
bool ComputeDecalUsesTopPlane(bool hitCeiling);

// [rc4l] The height to draw a decal at, this frame.
//
// Anchored to where the bullet LANDED, then moved by however far its surface has travelled since.
// Reading the plane's current height directly instead is wrong whenever the hit was not on the
// sector's own floor: a shot landing on a 3D floor 192 units up reports a hit there while the
// sector's floor is still at 0, and the decal is drawn 192 units under the surface it marked.
//
// The offset lifts the quad clear of the surface so it is never swallowed by it, and points the
// opposite way for a ceiling -- down, into the room -- because "away from the surface" is downward
// there.
float ComputeDecalHeight(float hitZ, float planeZAtSpawn, float planeZNow, bool ceiling,
                         float offset);

}} // namespace zx::levelmesh

#endif // ZX_FLATDECAL_COMPUTE_H
