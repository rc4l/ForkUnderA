// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/models/computation/objmodel_compute.h"

#include <cstring>
#include <gtest/gtest.h>

using namespace zx::models;

namespace {

struct Parsed
{
	ObjData data;
	ObjProblems problems;
	bool ok;
};

Parsed Parse(const char *text)
{
	Parsed p;
	p.ok = ComputeObjParse(text, std::strlen(text), p.data, p.problems);
	return p;
}

std::vector<ObjDrawVertex> Triangles(const ObjData &data, std::vector<unsigned> &first, std::vector<unsigned> &count)
{
	std::vector<ObjDrawVertex> out;
	ComputeObjTriangles(data, out, first, count);
	return out;
}

// [Doruk-Aksoy] A unit square's corners and texture coordinates, which the face tests share.
const char *const kSquare =
	"v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
	"vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n";

} // namespace

TEST(ObjParse, PositionsCoordinatesAndOneTriangle)
{
	const Parsed p = Parse("v 1 2 3\nv 4 5 6 1.0\nv 7 8 9\nvt 0.25 0.75\nf 1/1 2/1 3/1\n");
	ASSERT_TRUE(p.ok);
	EXPECT_EQ((std::vector<float>{ 1, 2, 3, 4, 5, 6, 7, 8, 9 }), p.data.positions) << "a w past x, y, z is left out";
	EXPECT_EQ((std::vector<float>{ 0.25f, 0.75f }), p.data.uvs);
	ASSERT_EQ(1u, p.data.faceStart.size());
	ASSERT_EQ(3u, p.data.corners.size());
	EXPECT_EQ(2, p.data.corners[2].vertex);
	EXPECT_EQ(0, p.data.corners[2].uv);
	ASSERT_EQ(1u, p.data.surfaces.size());
	EXPECT_EQ("", p.data.surfaces[0].material);
	EXPECT_EQ(0, p.problems.badLines + p.problems.droppedFaces + p.problems.badUVs);
}

// [Doruk-Aksoy] Every corner form OBJ has, negative indices counting back from the last line written.
TEST(ObjParse, EveryCornerFormAndNegativeIndices)
{
	const Parsed p = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 1\nvn 0 0 1\nf 1 2/2 3//1\nf -3/-2/1 -2/-1 -1//1\n");
	ASSERT_TRUE(p.ok);
	ASSERT_EQ(6u, p.data.corners.size());
	EXPECT_EQ(-1, p.data.corners[0].uv);
	EXPECT_EQ(1, p.data.corners[1].uv);
	EXPECT_EQ(-1, p.data.corners[2].uv);
	EXPECT_EQ(0, p.data.corners[3].vertex);
	EXPECT_EQ(0, p.data.corners[3].uv);
	EXPECT_EQ(2, p.data.corners[5].vertex);
}

// [Doruk-Aksoy] A corner may name a position written after its face, as UZDoom reads it at the end.
TEST(ObjParse, ACornerMayNameALaterPosition)
{
	const Parsed p = Parse("f 1 2 3\nv 0 0 0\nv 1 0 0\nv 0 1 0\n");
	EXPECT_TRUE(p.ok);
	EXPECT_EQ(0, p.problems.droppedFaces);
}

TEST(ObjParse, CommentsBlankLinesCrlfAndOtherStatementsAreSkipped)
{
	const Parsed p = Parse("# made by hand\r\n\r\nmtllib x.mtl\r\no thing\r\ng part\r\ns 1\r\nv 0 0 0 # inline\r\nv 1 0 0\r\nv 0 1 0\r\nl 1 2\r\nf 1 2 3\r\n#\r\n");
	ASSERT_TRUE(p.ok);
	EXPECT_EQ(9u, p.data.positions.size());
	EXPECT_EQ(1u, p.data.faceStart.size());
	EXPECT_EQ(0, p.problems.badLines);
}

// [Doruk-Aksoy] Each usemtl starts a surface once the last has faces, and a name with spaces keeps them.
TEST(ObjParse, UsemtlSplitsSurfacesAndRenamesAnEmptyOne)
{
	const Parsed p = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\nusemtl first\nusemtl textures/my wall.png\nf 1 2 3\nf 1 2 3\nusemtl STARTAN2\nf 3 2 1\n");
	ASSERT_TRUE(p.ok);
	ASSERT_EQ(3u, p.data.surfaces.size());
	EXPECT_EQ("", p.data.surfaces[0].material) << "faces before any usemtl keep a surface of their own";
	EXPECT_EQ("textures/my wall.png", p.data.surfaces[1].material);
	EXPECT_EQ(1u, p.data.surfaces[1].firstFace);
	EXPECT_EQ(2u, p.data.surfaces[1].faceCount);
	EXPECT_EQ("STARTAN2", p.data.surfaces[2].material);
	EXPECT_EQ(3u, p.data.surfaces[2].firstFace);
}

TEST(ObjParse, AUsemtlWithNoNameIsABadLine)
{
	const Parsed p = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl\nf 1 2 3\n");
	ASSERT_TRUE(p.ok);
	EXPECT_EQ(1, p.problems.badLines);
	EXPECT_EQ("", p.data.surfaces[0].material);
}

// [Doruk-Aksoy] UZDoom read a corner past the positions straight out of its array; here the face is dropped and the rest still load.
TEST(ObjParse, AFacePastThePositionsIsDroppedNotRead)
{
	const Parsed p = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\nf 1 2 -4\nf 0 1 2\nf 1 2 3\n");
	ASSERT_TRUE(p.ok);
	EXPECT_EQ(3, p.problems.droppedFaces);
	EXPECT_EQ(1u, p.data.faceStart.size());
	EXPECT_EQ(1u, p.data.surfaces[0].faceCount);
}

TEST(ObjParse, FacesUnderThreeCornersOrUnreadableAreDropped)
{
	const Parsed p = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf\nf 1 2\nf 1 x 3\nf 1/a 2 3\nf 1/1/1/1 2 3\nf 1 2 3\n");
	ASSERT_TRUE(p.ok);
	EXPECT_EQ(5, p.problems.droppedFaces);
	EXPECT_EQ(1u, p.data.faceStart.size());
}

// [Doruk-Aksoy] A texture coordinate past the file's is left out of its corner, which then takes the first, as UZDoom does.
TEST(ObjParse, ACoordinatePastTheFilesIsLeftOut)
{
	const Parsed p = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nf 1/1 2/5 3/-9\n");
	ASSERT_TRUE(p.ok);
	EXPECT_EQ(2, p.problems.badUVs);
	EXPECT_EQ(-1, p.data.corners[1].uv);
	EXPECT_EQ(-1, p.data.corners[2].uv);
}

// [Doruk-Aksoy] A position or coordinate line that cannot be read still takes its place, with 0s, so the indices after it keep naming what the exporter meant.
TEST(ObjParse, BadNumberLinesKeepTheirPlace)
{
	const Parsed p = Parse("v 1 2\nv a 0 0\nv 0 1 0\nvt\nvt 0.5\nvt x 1\nf 1 2 3\n");
	ASSERT_TRUE(p.ok);
	EXPECT_EQ(4, p.problems.badLines) << "v short, v unreadable, vt empty, vt unreadable";
	EXPECT_EQ((std::vector<float>{ 1, 2, 0, 0, 0, 0, 0, 1, 0 }), p.data.positions);
	EXPECT_EQ((std::vector<float>{ 0, 0, 0.5f, 0, 0, 1 }), p.data.uvs) << "a lone u takes v 0";
}

// [Doruk-Aksoy] An infinite or not-a-number coordinate is unreadable, so no position takes it.
TEST(ObjParse, InfiniteNumbersAreUnreadable)
{
	const Parsed p = Parse("v inf 0 0\nv 0 nan 0\nv 1 0 0\nf 1 2 3\n");
	ASSERT_TRUE(p.ok);
	EXPECT_EQ(2, p.problems.badLines);
	EXPECT_EQ((std::vector<float>{ 0, 0, 0, 0, 0, 0, 1, 0, 0 }), p.data.positions);
}

TEST(ObjParse, NoFaceLeftIsNoModel)
{
	EXPECT_FALSE(Parse("").ok);
	EXPECT_FALSE(Parse("v 0 0 0\nv 1 0 0\n# no faces\n").ok);
	EXPECT_FALSE(Parse("v 0 0 0\nf 1 2 3\n").ok);
	const Parsed p = Parse("\x01\xff garbage\n\n");
	EXPECT_FALSE(p.ok);
	EXPECT_TRUE(p.data.surfaces.empty());
}

// [Doruk-Aksoy] UZDoom's FOBJModel::BuildVertexBuffer: z negated, v flipped, and each triangle's corners last to first.
TEST(ObjTriangles, ATriangleAsUZDoomLaysItOut)
{
	const Parsed p = Parse("v 1 2 3\nv 4 5 6\nv 7 8 9\nvt 0.1 0.2\nvt 0.3 0.4\nvt 0.5 0.6\nf 1/1 2/2 3/3\n");
	std::vector<unsigned> first, count;
	const std::vector<ObjDrawVertex> t = Triangles(p.data, first, count);
	ASSERT_EQ(3u, t.size());
	EXPECT_FLOAT_EQ(7.f, t[0].x);
	EXPECT_FLOAT_EQ(8.f, t[0].y);
	EXPECT_FLOAT_EQ(-9.f, t[0].z);
	EXPECT_FLOAT_EQ(0.5f, t[0].u);
	EXPECT_FLOAT_EQ(0.4f, t[0].v);
	EXPECT_FLOAT_EQ(4.f, t[1].x);
	EXPECT_FLOAT_EQ(1.f, t[2].x);
	EXPECT_FLOAT_EQ(0.8f, t[2].v);
	EXPECT_EQ((std::vector<unsigned>{ 0 }), first);
	EXPECT_EQ((std::vector<unsigned>{ 3 }), count);
}

// [Doruk-Aksoy] A quad splits 0-1-3 and 1-2-3 as UZDoom's TriangulateQuad does, so a bent quad bends the same way.
TEST(ObjTriangles, AQuadSplitsAsUZDoomSplitsIt)
{
	const Parsed p = Parse((std::string(kSquare) + "f 1/1 2/2 3/3 4/4\n").c_str());
	std::vector<unsigned> first, count;
	const std::vector<ObjDrawVertex> t = Triangles(p.data, first, count);
	ASSERT_EQ(6u, t.size());
	// [Doruk-Aksoy] Corners 0, 1, 3 written 3, 1, 0, then 1, 2, 3 written 3, 2, 1.
	const float xs[6] = { 0, 1, 0, 0, 1, 1 }, ys[6] = { 1, 0, 0, 1, 1, 0 };
	for (int i = 0; i < 6; i++)
	{
		EXPECT_FLOAT_EQ(xs[i], t[i].x) << i;
		EXPECT_FLOAT_EQ(ys[i], t[i].y) << i;
	}
}

// [Doruk-Aksoy] UZDoom read the first four corners of a larger face and dropped the rest; here it fans from its first corner.
TEST(ObjTriangles, ALargerFaceFansFromItsFirstCorner)
{
	const Parsed p = Parse("v 0 0 0\nv 1 0 0\nv 2 1 0\nv 1 2 0\nv 0 2 0\nv -1 1 0\nf 1 2 3 4 5 6\n");
	std::vector<unsigned> first, count;
	const std::vector<ObjDrawVertex> t = Triangles(p.data, first, count);
	ASSERT_EQ(12u, t.size());
	for (int tri = 0; tri < 4; tri++)
	{
		EXPECT_FLOAT_EQ(0.f, t[tri * 3 + 2].x) << "corner 0 closes every triangle, " << tri;
		EXPECT_FLOAT_EQ(0.f, t[tri * 3 + 2].y);
	}
	EXPECT_FLOAT_EQ(-1.f, t[9].x) << "the last triangle ends at the last corner";
}

// [Doruk-Aksoy] A corner with no coordinate takes the file's first, and with none in the file (0, 0), each with v flipped.
TEST(ObjTriangles, ACornerWithNoCoordinateTakesTheFirstOrNone)
{
	std::vector<unsigned> first, count;
	const Parsed with = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.25 0.5\nvt 1 1\nf 1 2 3\n");
	const std::vector<ObjDrawVertex> a = Triangles(with.data, first, count);
	EXPECT_FLOAT_EQ(0.25f, a[0].u);
	EXPECT_FLOAT_EQ(0.5f, a[0].v);
	const Parsed without = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
	const std::vector<ObjDrawVertex> b = Triangles(without.data, first, count);
	EXPECT_FLOAT_EQ(0.f, b[0].u);
	EXPECT_FLOAT_EQ(1.f, b[0].v);
}

TEST(ObjTriangles, EachSurfaceItsOwnRun)
{
	const Parsed p = Parse((std::string(kSquare) + "usemtl a\nf 1 2 3\nusemtl b\nf 1 2 3 4\nf 2 3 4\n").c_str());
	std::vector<unsigned> first, count;
	const std::vector<ObjDrawVertex> t = Triangles(p.data, first, count);
	EXPECT_EQ(12u, t.size());
	EXPECT_EQ((std::vector<unsigned>{ 0, 3 }), first);
	EXPECT_EQ((std::vector<unsigned>{ 3, 9 }), count);
}

TEST(ObjTriangles, NoSurfacesNoTriangles)
{
	std::vector<unsigned> first = { 7 }, count = { 7 };
	const std::vector<ObjDrawVertex> t = Triangles(ObjData(), first, count);
	EXPECT_TRUE(t.empty());
	EXPECT_TRUE(first.empty());
	EXPECT_TRUE(count.empty());
}
