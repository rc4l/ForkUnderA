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
// Anchored to where the bullet LANDED, then moved by however far its surface has travelled since --
// but only when that plane is demonstrably the surface in question. See ComputeDecalTracksPlane.
//
// Reading the plane's current height directly instead is wrong whenever the hit was not on the
// sector's own floor: a shot landing on a 3D floor 192 units up reports a hit there while the
// sector's floor is still at 0, and the decal is drawn 192 units under the surface it marked.

// [rc4l] Is the plane we picked actually the surface the shot landed on?
//
// It is only safe to move a decal with a plane if that plane IS its surface, and the way to know is
// that the plane was at the hit height when the shot landed. A shot can land at 128 on geometry the
// trace does not report as a 3D floor -- trace.ffloor comes back NULL -- while the sector's own
// floor is somewhere else entirely. Tracking that plane then teleports the decal to it: measured at
// hit 128, plane 128 at spawn and 0 at render, decal drawn at 0.1.
//
// When this returns false the decal simply stays where the bullet hit, which is right for anything
// that does not move and no worse than the alternative for anything that does.
bool ComputeDecalTracksPlane(float hitZ, float planeZAtSpawn, float tolerance);

// The offset lifts the quad clear of the surface so it is never swallowed by it, and points the
// opposite way for a ceiling -- down, into the room -- because "away from the surface" is downward
// there.
float ComputeDecalHeight(float hitZ, float planeZAtSpawn, float planeZNow, bool ceiling,
                         float offset, float trackTolerance);

}} // namespace zx::levelmesh

#endif // ZX_FLATDECAL_COMPUTE_H
