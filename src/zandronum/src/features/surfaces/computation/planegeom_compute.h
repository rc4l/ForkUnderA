// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A sector plane as a surface, on the same terms as a sidedef part.
//
// This is the other half of retiring the wall/flat split. `wallgeom_compute` answers "where is this
// sidedef part" and `walluv_compute` answers "what is printed on it"; these answer both for a floor
// or a ceiling. Same shape of question, same kind of answer, one namespace -- which is the point:
// the split is not in the data, it is in there being two derivations.
//
// The plane math that already existed lives in features/levelmesh/computation/flatmesh_compute and
// stays there for now, because it is what the CAPTURE path uses and moving it would be churn. What
// is here is what a derivation needs and that one does not: the height of a plane at a point, and
// whether a plane faces the viewer -- both stated so a floor and a wall can be asked the same
// questions by the same caller.
//
// Engine-free: a plane is four numbers, exactly as ZDoom stores it.

#ifndef ZX_PLANEGEOM_COMPUTE_H
#define ZX_PLANEGEOM_COMPUTE_H

namespace zx { namespace surfaces {

// ZDoom's plane equation: a*x + b*y + c*z + d = 0, normalised so c is negative for a floor and
// positive for a ceiling. A level plane has a == b == 0.
struct SurfacePlane
{
	float a, b, c, d;
};

// The plane's height at a map point. This is secplane_t::ZatPoint, and it is the one operation a
// sloped surface needs that a level one does not.
float ComputePlaneHeightAt(const SurfacePlane &p, float x, float y);

// Is this plane sloped at all?
//
// Asked constantly, and worth its own name: nearly every rule about a plane has a cheap answer when
// the plane is level and a general one when it is not, and the general one is where the bugs are.
bool ComputePlaneIsSloped(const SurfacePlane &p);

// [rc4l] Does this plane face a viewer at this height, at this point?
//
// A floor is seen from above and a ceiling from below, and Doom draws neither from the wrong side --
// which is not a nicety: the two planes of a sector are the same rectangle wound opposite ways, so
// drawing both regardless doubles the surface count and lets the two fight for the depth buffer.
//
// Stated in terms of the plane rather than "is it a floor", because a 3D floor's underside IS a
// floor plane seen from below, and every rule written as "floors face up" gets that one wrong.
bool ComputePlaneFacesViewer(const SurfacePlane &p, float x, float y, float viewerZ);

// The unit normal, in the mesh's (x, z-up, y) space, facing the side the surface is SEEN from.
//
// The same convention wallgeom's callers use, so a wall normal and a plane normal can be compared,
// sorted and lit by the same code -- which is the whole of "one surface type" at the arithmetic
// level.
void ComputePlaneNormal(const SurfacePlane &p, bool seenFromBelow, float outNormal[3]);

}} // namespace zx::surfaces

#endif // ZX_PLANEGEOM_COMPUTE_H
