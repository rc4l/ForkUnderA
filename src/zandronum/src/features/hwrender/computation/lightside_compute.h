// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Does this dynamic light reach this flat, and does the port agree with GL about it.
//
// gl_flats.cpp drops a light when the plane, evaluated at the LIGHT's own x and y, is on the wrong
// side of it. One comparison decides whether a whole surface is lit or not lit, with no middle: get
// it wrong and a floor goes completely black under a light that is sitting on it, along a dead
// straight line at the piece boundary, in one renderer only.
//
// The port cannot run gl_flats.cpp's test -- the shader has a baked normal and a plane constant, not
// a secplane_t -- so it computes an equivalent expression, and "equivalent" is the whole risk. The
// two forms are in here together precisely so a test can assert they agree, including at the
// boundary, which is where they did not:
//
//   GL keeps a light when  a*lx + b*ly + c*lz + d <= 0  (strictly-greater is what it drops on)
//   the port keeps it when dot(n, light) - planeD >= 0
//
// and the mesh normal being sign*(a, c, b)/len makes the second the negative of the first, so they
// are the same test written twice. They still disagreed, because a projectile that dies on a floor
// comes to REST on it: its z equals the plane there to the last bit of fixed point, GL's <= keeps
// it, and the port's float arithmetic put the exact zero a few thousandths on the wrong side.
//
// Header-pure and engine-free, so the rule is unit-tested off-engine.

#ifndef ZX_LIGHTSIDE_COMPUTE_H
#define ZX_LIGHTSIDE_COMPUTE_H

namespace zx { namespace hwrender {

// A sector plane in ZDoom's convention: a*x + b*y + c*z + d = 0, with c negative for a floor.
struct SecPlaneF
{
	float a, b, c, d;
};

// The plane's height at a map point. Mirrors secplane_t::ZatPoint.
float ComputePlaneZAt(const SecPlaneF &p, float x, float y);

// GL's own test, from gl_flats.cpp: the plane height at the light's own point against the light's
// height. A light lying EXACTLY in the plane is kept, because the comparison there is strict.
bool ComputeGLLightReaches(const SecPlaneF &p, float lx, float ly, float lz, bool ceiling);

// The surface normal the mesh bakes, in its own (x, z-up, y) space, together with the plane constant
// the vertex shader derives as dot(normal, position). Turned to face the side the surface is SEEN
// from, which is not always the way its plane points -- a 3D floor's underside is the control
// sector's floor plane, so its normal points up while the surface is looked at from below.
void ComputeMeshPlane(const SecPlaneF &p, bool viewedFromBelow, float outNormal[3], float *outPlaneD);

// How far behind the plane a light may sit and still be kept.
//
// Not a tuning knob and not an attempt to place the boundary exactly, which floats cannot do. It is
// slack for the case Doom produces constantly -- a light resting exactly ON a surface -- so that the
// port lands on the same side of the boundary as GL. Thousands of times the float error being
// covered, and far below anything a player could see.
extern const float kLightOnPlaneSlack;

// The port's test, as the shader runs it. Positions are in the mesh's (x, z-up, y) space.
bool ComputeShaderLightReaches(const float normal[3], float planeD, const float lightMesh[3]);

} }   // namespace zx::hwrender

#endif
