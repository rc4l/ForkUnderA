// [MGOOOOOO] Tests for the debug hitbox overlay's wireframe geometry.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "gtest/gtest.h"

#include <set>
#include <utility>

#include "features/hitboxviz/computation/boxedges_compute.h"

using namespace zx::hitboxviz;

namespace
{
	// Collects the distinct corner positions touched by a vertex list, so a test can assert on the
	// set of corners without depending on the order edges happen to be emitted in.
	std::set<std::pair<std::pair<float, float>, float> > CornerSet(const Vertex3 *verts, unsigned int count)
	{
		std::set<std::pair<std::pair<float, float>, float> > corners;
		for (unsigned int i = 0; i < count; ++i)
			corners.insert(std::make_pair(std::make_pair(verts[i].x, verts[i].y), verts[i].z));
		return corners;
	}

	// Number of distinct edges, treating an edge as undirected.
	std::set<std::pair<unsigned int, unsigned int> > EdgeSet(const Vertex3 *verts, unsigned int count,
		const std::set<std::pair<std::pair<float, float>, float> > &corners)
	{
		std::set<std::pair<unsigned int, unsigned int> > edges;
		for (unsigned int i = 0; i + 1 < count; i += 2)
		{
			unsigned int a = 0, b = 0, idx = 0;
			for (std::set<std::pair<std::pair<float, float>, float> >::const_iterator it = corners.begin();
				it != corners.end(); ++it, ++idx)
			{
				if (it->first.first == verts[i].x && it->first.second == verts[i].y && it->second == verts[i].z)
					a = idx;
				if (it->first.first == verts[i + 1].x && it->first.second == verts[i + 1].y && it->second == verts[i + 1].z)
					b = idx;
			}
			edges.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
		}
		return edges;
	}
}

// ---- BuildBoxEdges ---------------------------------------------------------

TEST(HitboxVizBoxEdges, EmitsTwelveEdgesAsTwentyFourVertices)
{
	Vertex3 verts[BOX_EDGE_VERTS];
	const unsigned int count = BuildBoxEdges(0.f, 0.f, 0.f, 16.f, 56.f, verts);

	EXPECT_EQ(static_cast<unsigned int>(BOX_EDGE_VERTS), count);
	EXPECT_EQ(24u, count);

	// A box has 12 distinct undirected edges; nothing should be emitted twice.
	const std::set<std::pair<std::pair<float, float>, float> > corners = CornerSet(verts, count);
	EXPECT_EQ(12u, EdgeSet(verts, count, corners).size());
}

TEST(HitboxVizBoxEdges, TouchesExactlyTheEightCorners)
{
	// A Doom player: radius 16, height 56, standing on the floor at z=0.
	Vertex3 verts[BOX_EDGE_VERTS];
	const unsigned int count = BuildBoxEdges(100.f, 200.f, 0.f, 16.f, 56.f, verts);

	const std::set<std::pair<std::pair<float, float>, float> > corners = CornerSet(verts, count);
	ASSERT_EQ(8u, corners.size());

	for (int cx = 0; cx < 2; ++cx)
	{
		for (int cy = 0; cy < 2; ++cy)
		{
			for (int cz = 0; cz < 2; ++cz)
			{
				const float x = 100.f + (cx ? 16.f : -16.f);
				const float y = 200.f + (cy ? 16.f : -16.f);
				const float z = cz ? 56.f : 0.f;
				EXPECT_EQ(1u, corners.count(std::make_pair(std::make_pair(x, y), z)))
					<< "missing corner " << x << "," << y << "," << z;
			}
		}
	}
}

TEST(HitboxVizBoxEdges, SpansBottomZToBottomZPlusHeight)
{
	// The box grows upward from the actor's z, matching how radius/height bound an actor.
	Vertex3 verts[BOX_EDGE_VERTS];
	const unsigned int count = BuildBoxEdges(0.f, 0.f, 128.f, 20.f, 64.f, verts);

	float minZ = verts[0].z, maxZ = verts[0].z;
	for (unsigned int i = 1; i < count; ++i)
	{
		if (verts[i].z < minZ) minZ = verts[i].z;
		if (verts[i].z > maxZ) maxZ = verts[i].z;
	}

	EXPECT_FLOAT_EQ(128.f, minZ);
	EXPECT_FLOAT_EQ(192.f, maxZ);
}

TEST(HitboxVizBoxEdges, RadiusAppliesToBothHorizontalAxes)
{
	Vertex3 verts[BOX_EDGE_VERTS];
	const unsigned int count = BuildBoxEdges(0.f, 0.f, 0.f, 31.f, 40.f, verts);

	for (unsigned int i = 0; i < count; ++i)
	{
		EXPECT_FLOAT_EQ(31.f, verts[i].x < 0.f ? -verts[i].x : verts[i].x);
		EXPECT_FLOAT_EQ(31.f, verts[i].y < 0.f ? -verts[i].y : verts[i].y);
	}
}

TEST(HitboxVizBoxEdges, EveryEdgeIsAxisAligned)
{
	// Each edge must vary along exactly one axis, or the wireframe is not a box.
	Vertex3 verts[BOX_EDGE_VERTS];
	const unsigned int count = BuildBoxEdges(5.f, -7.f, 3.f, 12.f, 24.f, verts);

	for (unsigned int i = 0; i + 1 < count; i += 2)
	{
		int varying = 0;
		if (verts[i].x != verts[i + 1].x) ++varying;
		if (verts[i].y != verts[i + 1].y) ++varying;
		if (verts[i].z != verts[i + 1].z) ++varying;
		EXPECT_EQ(1, varying) << "edge " << (i / 2) << " is not axis-aligned";
	}
}

TEST(HitboxVizBoxEdges, DegenerateBoxEmitsNothing)
{
	Vertex3 verts[BOX_EDGE_VERTS];

	EXPECT_EQ(0u, BuildBoxEdges(0.f, 0.f, 0.f,  0.f, 56.f, verts));
	EXPECT_EQ(0u, BuildBoxEdges(0.f, 0.f, 0.f, 16.f,  0.f, verts));
	EXPECT_EQ(0u, BuildBoxEdges(0.f, 0.f, 0.f,  0.f,  0.f, verts));
	// Negative extents are nonsense rather than a mirrored box.
	EXPECT_EQ(0u, BuildBoxEdges(0.f, 0.f, 0.f, -16.f, 56.f, verts));
	EXPECT_EQ(0u, BuildBoxEdges(0.f, 0.f, 0.f, 16.f, -56.f, verts));
}

TEST(HitboxVizBoxEdges, DoesNotWriteBeyondReportedCount)
{
	// Guards the caller's buffer accounting: a degenerate box must leave the buffer untouched.
	Vertex3 verts[BOX_EDGE_VERTS];
	for (int i = 0; i < BOX_EDGE_VERTS; ++i)
	{
		verts[i].x = 1234.f;
		verts[i].y = 5678.f;
		verts[i].z = 9012.f;
	}

	EXPECT_EQ(0u, BuildBoxEdges(0.f, 0.f, 0.f, 0.f, 0.f, verts));
	EXPECT_FLOAT_EQ(1234.f, verts[0].x);
	EXPECT_FLOAT_EQ(5678.f, verts[0].y);
	EXPECT_FLOAT_EQ(9012.f, verts[0].z);
}

// ---- BuildPlaneMarker ------------------------------------------------------

TEST(HitboxVizPlaneMarker, EmitsTwoDiagonalsAtTheGivenHeight)
{
	Vertex3 verts[PLANE_MARKER_VERTS];
	const unsigned int count = BuildPlaneMarker(10.f, 20.f, 42.f, 16.f, verts);

	ASSERT_EQ(static_cast<unsigned int>(PLANE_MARKER_VERTS), count);
	ASSERT_EQ(4u, count);

	for (unsigned int i = 0; i < count; ++i)
		EXPECT_FLOAT_EQ(42.f, verts[i].z);

	// Two crossing diagonals: opposite corners of the square.
	EXPECT_FLOAT_EQ(-6.f, verts[0].x);  EXPECT_FLOAT_EQ( 4.f, verts[0].y);
	EXPECT_FLOAT_EQ(26.f, verts[1].x);  EXPECT_FLOAT_EQ(36.f, verts[1].y);
	EXPECT_FLOAT_EQ(-6.f, verts[2].x);  EXPECT_FLOAT_EQ(36.f, verts[2].y);
	EXPECT_FLOAT_EQ(26.f, verts[3].x);  EXPECT_FLOAT_EQ( 4.f, verts[3].y);
}

TEST(HitboxVizPlaneMarker, DegenerateRadiusEmitsNothing)
{
	Vertex3 verts[PLANE_MARKER_VERTS];
	EXPECT_EQ(0u, BuildPlaneMarker(0.f, 0.f, 0.f,  0.f, verts));
	EXPECT_EQ(0u, BuildPlaneMarker(0.f, 0.f, 0.f, -8.f, verts));
}

// ---- ComputeBlastPrism -----------------------------------------------------

TEST(HitboxVizBlastPrism, IsACubeCenteredOnTheBlastZ)
{
	// The damage pattern is square (Chebyshev), and the vertical reach is the same distance, so the
	// region is [x +/- d] x [y +/- d] x [z - d, z + d]. A sphere here would be wrong.
	const BlastPrism prism = ComputeBlastPrism(100.f, 200.f, 50.f, 128.f);

	EXPECT_FLOAT_EQ(100.f, prism.centerX);
	EXPECT_FLOAT_EQ(200.f, prism.centerY);
	EXPECT_FLOAT_EQ(128.f, prism.radius);
	EXPECT_FLOAT_EQ(-78.f, prism.bottomZ);       // 50 - 128
	EXPECT_FLOAT_EQ(256.f, prism.height);        // 2 * 128
	EXPECT_FLOAT_EQ(178.f, prism.bottomZ + prism.height); // 50 + 128
}

TEST(HitboxVizBlastPrism, FeedsStraightIntoBuildBoxEdges)
{
	const BlastPrism prism = ComputeBlastPrism(0.f, 0.f, 0.f, 64.f);

	Vertex3 verts[BOX_EDGE_VERTS];
	const unsigned int count = BuildBoxEdges(prism.centerX, prism.centerY, prism.bottomZ,
		prism.radius, prism.height, verts);

	EXPECT_EQ(24u, count);

	const std::set<std::pair<std::pair<float, float>, float> > corners = CornerSet(verts, count);
	EXPECT_EQ(8u, corners.size());
	EXPECT_EQ(1u, corners.count(std::make_pair(std::make_pair(-64.f, -64.f), -64.f)));
	EXPECT_EQ(1u, corners.count(std::make_pair(std::make_pair( 64.f,  64.f),  64.f)));
}

// ---- ClampFullDamageDistance ----------------------------------------------

TEST(HitboxVizFullDamageClamp, MatchesTheEngineClamp)
{
	// p_map.cpp: clamp<int>(fulldamagedistance, 0, bombdistance - 1)
	EXPECT_EQ(0,   ClampFullDamageDistance(128, 0));
	EXPECT_EQ(64,  ClampFullDamageDistance(128, 64));
	EXPECT_EQ(127, ClampFullDamageDistance(128, 127));
}

TEST(HitboxVizFullDamageClamp, ClampsAtOrAboveTheOuterDistance)
{
	// The inner region can never reach the outer edge, or the falloff would divide by zero.
	EXPECT_EQ(127, ClampFullDamageDistance(128, 128));
	EXPECT_EQ(127, ClampFullDamageDistance(128, 9999));
}

TEST(HitboxVizFullDamageClamp, ClampsNegativeToZero)
{
	EXPECT_EQ(0, ClampFullDamageDistance(128, -1));
	EXPECT_EQ(0, ClampFullDamageDistance(128, -9999));
}

TEST(HitboxVizFullDamageClamp, NonPositiveDistanceHasNoInnerRegion)
{
	// P_RadiusAttack returns immediately for distance <= 0, so there is no blast to draw.
	EXPECT_EQ(0, ClampFullDamageDistance(0, 10));
	EXPECT_EQ(0, ClampFullDamageDistance(-5, 10));
	EXPECT_EQ(0, ClampFullDamageDistance(1, 10));  // distance - 1 == 0
}
