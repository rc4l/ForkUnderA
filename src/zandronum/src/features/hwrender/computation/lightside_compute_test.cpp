// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The shipped fault these exist for: a plasma bolt lying on a sloped floor lit the walkway
// above it and left the floor it was resting on completely black, along a dead straight line, in
// Vulkan only. Measured on dbab04 as GL 30.6 against Vulkan 0.0 over the same rectangle of the same
// frozen frame.
//
// The numbers below are that frame's: the floor plane read -59.053 under the light's own point, and
// the light's z read -59.053. Equal to three decimals is not a coincidence -- a projectile that dies
// on a floor comes to rest ON it -- and it is the whole bug.

#include <gtest/gtest.h>

#include <math.h>

#include "features/hwrender/computation/lightside_compute.h"

using namespace zx::hwrender;

namespace {

// A level floor at height h. ZDoom normalises a floor with c == -1, so z = -d/c = d.
SecPlaneF LevelFloor(float h)
{
	SecPlaneF p;
	p.a = 0.f; p.b = 0.f; p.c = -1.f; p.d = h;
	return p;
}

// A level ceiling at height h: the plane points the other way.
SecPlaneF LevelCeiling(float h)
{
	SecPlaneF p;
	p.a = 0.f; p.b = 0.f; p.c = 1.f; p.d = -h;
	return p;
}

// A floor tilted along x, passing through (0,0,h). Normalised so that (a,b,c) is a unit vector,
// which is what P_AlignPlane leaves behind.
SecPlaneF SlopedFloor(float h, float slope)
{
	const float len = sqrtf(slope * slope + 1.f);
	SecPlaneF p;
	p.a = slope / len;
	p.b = 0.f;
	p.c = -1.f / len;
	p.d = h / len;
	return p;
}

// Run the port's chain end to end: bake the plane the way the mesh does, then ask the shader's test.
bool PortReaches(const SecPlaneF &p, bool viewedFromBelow, float lx, float ly, float lz)
{
	float n[3], planeD;
	ComputeMeshPlane(p, viewedFromBelow, n, &planeD);
	const float lightMesh[3] = { lx, lz, ly };   // the mesh is (x, z-up, y)
	return ComputeShaderLightReaches(n, planeD, lightMesh);
}

}   // namespace

// ---- the plane's own arithmetic --------------------------------------------

TEST(LightSideCompute, ALevelFloorHasTheSameHeightEverywhere)
{
	const SecPlaneF f = LevelFloor(-59.053f);
	EXPECT_FLOAT_EQ(ComputePlaneZAt(f, 0.f, 0.f), -59.053f);
	EXPECT_FLOAT_EQ(ComputePlaneZAt(f, -1105.4f, -268.4f), -59.053f);
}

TEST(LightSideCompute, ASlopedFloorRisesAcrossItself)
{
	const SecPlaneF s = SlopedFloor(0.f, 0.5f);
	EXPECT_NEAR(ComputePlaneZAt(s, 0.f, 0.f), 0.f, 0.001f);
	EXPECT_NEAR(ComputePlaneZAt(s, 100.f, 0.f), 50.f, 0.01f);
	EXPECT_NEAR(ComputePlaneZAt(s, -100.f, 0.f), -50.f, 0.01f);
}

// ---- a floor's normal must end up pointing UP ------------------------------

TEST(LightSideCompute, AFloorSeenFromAboveGetsAnUpwardNormal)
{
	float n[3], d;
	ComputeMeshPlane(LevelFloor(-59.f), /*viewedFromBelow=*/false, n, &d);
	EXPECT_GT(n[1], 0.9f);   // mesh y is up
}

TEST(LightSideCompute, ACeilingSeenFromBelowGetsADownwardNormal)
{
	float n[3], d;
	ComputeMeshPlane(LevelCeiling(140.f), /*viewedFromBelow=*/true, n, &d);
	EXPECT_LT(n[1], -0.9f);
}

TEST(LightSideCompute, TheBakedPlaneConstantIsTheSameAtEveryPointOnTheSurface)
{
	// vPlane.w is dot(normal, position) taken from ONE vertex and held flat across the triangle, so
	// it is only meaningful if every point on the plane gives the same number.
	const SecPlaneF s = SlopedFloor(-20.f, 0.35f);
	float n[3], d;
	ComputeMeshPlane(s, false, n, &d);
	for (float x = -400.f; x <= 400.f; x += 137.f)
	{
		const float z = ComputePlaneZAt(s, x, 0.f);
		const float dot = n[0] * x + n[1] * z + n[2] * 0.f;
		EXPECT_NEAR(dot, d, 0.01f) << "at x " << x;
	}
}

// ---- the case that shipped broken ------------------------------------------

TEST(LightSideCompute, ALightRestingExactlyOnAFloorIsKept)
{
	// The bug, in one test. A bolt that dies on a floor has z == the plane there, GL keeps it, and
	// the port must too.
	const float h = -59.053f;
	const SecPlaneF f = LevelFloor(h);
	EXPECT_TRUE(ComputeGLLightReaches(f, -1105.4f, -268.4f, h, false));
	EXPECT_TRUE(PortReaches(f, false, -1105.4f, -268.4f, h));
}

TEST(LightSideCompute, ALightRestingExactlyOnASlopedFloorIsKept)
{
	// The floor it actually happened on was sloped, so the plane constant is not the height and the
	// two forms have more room to disagree.
	const SecPlaneF s = SlopedFloor(-59.053f, 0.42f);
	const float lx = -105.4f, ly = -68.4f;
	const float lz = ComputePlaneZAt(s, lx, ly);
	EXPECT_TRUE(ComputeGLLightReaches(s, lx, ly, lz, false));
	EXPECT_TRUE(PortReaches(s, false, lx, ly, lz));
}

TEST(LightSideCompute, ALightRestingExactlyOnACeilingIsKept)
{
	const float h = 140.599f;
	const SecPlaneF c = LevelCeiling(h);
	EXPECT_TRUE(ComputeGLLightReaches(c, 10.f, 20.f, h, true));
	EXPECT_TRUE(PortReaches(c, true, 10.f, 20.f, h));
}

// ---- and the test must still do its job ------------------------------------

TEST(LightSideCompute, ALightWellUnderTheFloorIsDropped)
{
	// The slack must not turn the test off. A light a thousand units down is behind the surface and
	// lighting it is how the backs of walls and the room next door came out lit.
	const SecPlaneF f = LevelFloor(-59.f);
	EXPECT_FALSE(ComputeGLLightReaches(f, 0.f, 0.f, -1000.f, false));
	EXPECT_FALSE(PortReaches(f, false, 0.f, 0.f, -1000.f));
}

TEST(LightSideCompute, ALightAboveTheFloorIsKept)
{
	const SecPlaneF f = LevelFloor(-59.f);
	EXPECT_TRUE(ComputeGLLightReaches(f, 0.f, 0.f, 1000.f, false));
	EXPECT_TRUE(PortReaches(f, false, 0.f, 0.f, 1000.f));
}

TEST(LightSideCompute, TheSlackIsSmallEnoughToBeInvisible)
{
	// A tenth of a unit: thousands of times the float error it covers, and far below anything a
	// player could see. If this ever needs to grow, the derivation is wrong, not the tolerance.
	EXPECT_GT(kLightOnPlaneSlack, 0.f);
	EXPECT_LT(kLightOnPlaneSlack, 1.f);
}

// ---- the two forms are one test written twice ------------------------------

TEST(LightSideCompute, PortAndGLAgreeAcrossASweepOfLightHeights)
{
	// Swept past the boundary in steps far finer than the slack, on a slope, because the whole class
	// of fault is the two forms landing on opposite sides of one comparison.
	const SecPlaneF s = SlopedFloor(-59.053f, 0.42f);
	const float lx = -105.4f, ly = -68.4f;
	const float surface = ComputePlaneZAt(s, lx, ly);

	// [rc4l] The slack is measured PERPENDICULAR to the surface, not vertically, so on a slope it
	// covers a taller band of heights than its own value -- slack / cos(tilt). Skipping a vertical
	// 0.1 here instead reported a disagreement at dz -0.102 that was the port behaving correctly.
	float n[3];
	ComputeMeshPlane(s, false, n, NULL);
	const float band = kLightOnPlaneSlack / n[1];

	for (float dz = -40.f; dz <= 40.f; dz += 0.013f)
	{
		if (fabsf(dz) <= band) continue;   // the slack deliberately keeps this band
		const float lz = surface + dz;
		EXPECT_EQ(ComputeGLLightReaches(s, lx, ly, lz, false), PortReaches(s, false, lx, ly, lz))
			<< "at dz " << dz;
	}
}

TEST(LightSideCompute, PortAndGLAgreeOnACeilingToo)
{
	const SecPlaneF c = LevelCeiling(140.599f);
	for (float dz = -40.f; dz <= 40.f; dz += 0.017f)
	{
		if (fabsf(dz) <= kLightOnPlaneSlack) continue;
		const float lz = 140.599f + dz;
		EXPECT_EQ(ComputeGLLightReaches(c, 10.f, 20.f, lz, true), PortReaches(c, true, 10.f, 20.f, lz))
			<< "at dz " << dz;
	}
}

// ---- a billboard has no side -----------------------------------------------

TEST(LightSideCompute, AZeroNormalMeansTheCPUAlreadyDidThisSurface)
{
	// A sprite turns to face the camera and has no side for a light to be in front of or behind, so
	// it carries no normal and takes no light from this loop. A wall or a floor reaching here with a
	// zero normal takes NO dynamic light at all, which is why fua_mesh_verify checks for it.
	const float none[3] = { 0.f, 0.f, 0.f };
	const float light[3] = { 0.f, 100.f, 0.f };
	EXPECT_FALSE(ComputeShaderLightReaches(none, 0.f, light));
}
