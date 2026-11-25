// [Doruk-Aksoy] Tests for modelgpu_compute: resident geometry, a surface's record and reference, the vertex shader's corner against the CPU's, and the boxes.
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "features/levelmesh/computation/modelgpu_compute.h"
#include "features/levelmesh/computation/modelsurface_compute.h"
#include "features/hwrender/computation/spritepack_compute.h"

using namespace zx::levelmesh;

namespace {
const float kIdentity[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
// [Doruk-Aksoy] A yaw, a stretch and a move, as a model's pose gives one.
const float kPose[16] = { 0, 2, 0, 0,  -1, 0, 0, 0,  0, 0, 3, 0,  5, 6, 7, 1 };
// [Doruk-Aksoy] Two keyframes of a quad's four vertices, the second moved and turned.
const float kVerts[] = {
	0, 0, 0, 0.f, 0.f,   1, 0, 0, 1.f, 0.f,   0, 1, 0, 0.f, 1.f,   1, 1, 0, 1.f, 1.f,
	10, 0, 2, 0.5f, 0.5f,   11, 0, 2, 0.5f, 0.5f,   10, 1, 3, 0.5f, 0.5f,   11, 1, 3, 0.5f, 0.5f,
};
const unsigned kIndices[] = { 7, 0, 1, 2, 2, 1, 3 };

// [Doruk-Aksoy] A frame's resident words and stream words for one surface, laid out at awkward offsets as a busy frame would.
struct Frame
{
	std::vector<unsigned> geo, dyn;
	unsigned ref;
};

Frame Lay(bool indexed, unsigned base1, unsigned base2, float inter, unsigned firstIndex, const float *m, unsigned firstRef)
{
	Frame f;
	const unsigned at = 13;
	f.geo.assign(at + ComputeModelGeoWords(8, 7), 0xDEADu);
	const ModelGeoPlace place = ComputeModelGeoPack(kVerts, 8, kIndices, 7, at, &f.geo[at]);
	const unsigned matrixWord = 40, recordWord = 21;
	f.dyn.assign(matrixWord + 16, 0xBEEFu);
	memcpy(&f.dyn[matrixWord], m, 16 * sizeof(float));
	ModelRecordIn in = { firstRef, 99, place, indexed, base1, base2, firstIndex, inter, matrixWord };
	ComputeModelRecord(in, &f.dyn[recordWord]);
	f.ref = ComputeModelRef(recordWord);
	return f;
}

// [Doruk-Aksoy] The shader's corners for a run starting at firstRef against ComputeModelSurfaceCorners' for the same span.
void ExpectSameCorners(bool indexed, unsigned base1, unsigned base2, float inter, unsigned firstIndex, unsigned count, const float *m)
{
	const unsigned firstRef = 1000;
	const Frame f = Lay(indexed, base1, base2, inter, firstIndex, m, firstRef);
	float cpu[6 * 5];
	ASSERT_TRUE(ComputeModelSurfaceCorners(kVerts, 8, indexed ? kIndices : nullptr, indexed ? 7 : 0, { base1, base2, inter, firstIndex, count }, m, cpu));
	for (unsigned i = 0; i < count; i++)
	{
		float gpu[5];
		ASSERT_TRUE(ComputeModelCorner(f.dyn.data(), (unsigned)f.dyn.size(), f.geo.data(), (unsigned)f.geo.size(), f.ref, firstRef + i, gpu));
		for (unsigned c = 0; c < 5; c++) EXPECT_FLOAT_EQ(cpu[i * 5 + c], gpu[c]) << "corner " << i << " word " << c;
	}
}
}

// [Doruk-Aksoy] A geometry's vertices go first, its indices straight after, from the word it is placed at.
TEST(ModelGpu, GeometryPacksVerticesThenIndices)
{
	EXPECT_EQ(8u * 5 + 7, ComputeModelGeoWords(8, 7));
	EXPECT_EQ(0u, ComputeModelGeoWords(0, 0));
	std::vector<unsigned> out(ComputeModelGeoWords(8, 7));
	const ModelGeoPlace place = ComputeModelGeoPack(kVerts, 8, kIndices, 7, 100, out.data());
	EXPECT_EQ(100u, place.vertWord);
	EXPECT_EQ(140u, place.indexWord);
	float x;
	memcpy(&x, &out[5 * 4], sizeof(x));
	EXPECT_FLOAT_EQ(10.f, x);
	EXPECT_EQ(7u, out[40]);
	EXPECT_EQ(3u, out[46]);
	const ModelGeoPlace none = ComputeModelGeoPack(nullptr, 0, nullptr, 0, 5, nullptr);
	EXPECT_EQ(5u, none.vertWord);
	EXPECT_EQ(5u, none.indexWord);
}

// [Doruk-Aksoy] The record names the run, the piece, both keyframes' words, the lerp, where the indices start or that there are none, and the matrix.
TEST(ModelGpu, TheRecordHoldsTheSpan)
{
	unsigned w[kModelRecordWords];
	ComputeModelRecord({ 7, 3, { 100, 140 }, true, 4, 0, 1, 0.25f, 60 }, w);
	EXPECT_EQ(7u, w[0]);
	EXPECT_EQ(3u, w[1]);
	EXPECT_EQ(120u, w[2]);
	EXPECT_EQ(100u, w[3]);
	float inter;
	memcpy(&inter, &w[4], sizeof(inter));
	EXPECT_FLOAT_EQ(0.25f, inter);
	EXPECT_EQ(141u, w[5]);
	EXPECT_EQ(60u, w[6]);
	EXPECT_EQ(0u, w[7]);
	ComputeModelRecord({ 7, 3, { 100, 140 }, false, 4, 0, 1, 0.25f, 60 }, w);
	EXPECT_EQ(kModelSequential | 1u, w[5]);
}

// [Doruk-Aksoy] As ComputeModelSurfaceCorners, equal keyframes or no positive lerp blend nothing.
TEST(ModelGpu, TheRecordLerpsOnlyBetweenDifferentKeyframes)
{
	unsigned w[kModelRecordWords];
	float inter;
	ComputeModelRecord({ 0, 0, { 0, 0 }, false, 4, 4, 0, 0.5f, 0 }, w);
	memcpy(&inter, &w[4], sizeof(inter));
	EXPECT_EQ(0.f, inter);
	ComputeModelRecord({ 0, 0, { 0, 0 }, false, 0, 4, 0, -0.5f, 0 }, w);
	memcpy(&inter, &w[4], sizeof(inter));
	EXPECT_EQ(0.f, inter);
}

// [Doruk-Aksoy] A model reference sits between the general vertices' indices and the packed sprites' top bit, whatever word its record is at.
TEST(ModelGpu, TheReferenceCarriesItsFlag)
{
	EXPECT_EQ(0x40000000u, ComputeModelRef(0));
	EXPECT_EQ(0x40000000u | 123u, ComputeModelRef(123));
	EXPECT_EQ(0x7FFFFFFFu, ComputeModelRef(0xFFFFFFFFu));
}

// [Doruk-Aksoy] The vertex shader makes the corners the CPU made: indexed and in order, blended and not, under a pose.
TEST(ModelGpu, TheShadersCornersAreTheCpus)
{
	ExpectSameCorners(true, 0, 4, 0.3f, 1, 6, kPose);
	ExpectSameCorners(true, 4, 0, 0.75f, 1, 6, kIdentity);
	ExpectSameCorners(true, 0, 0, 0.5f, 1, 6, kPose);
	ExpectSameCorners(false, 0, 4, 0.6f, 0, 3, kPose);
	ExpectSameCorners(false, 4, 0, 0.f, 1, 3, kIdentity);
}

// [Doruk-Aksoy] A general vertex's or a packed sprite's reference is not a model's.
TEST(ModelGpu, OtherReferencesAreNotModels)
{
	const Frame f = Lay(true, 0, 4, 0.5f, 1, kIdentity, 0);
	float out[5];
	EXPECT_FALSE(ComputeModelCorner(f.dyn.data(), (unsigned)f.dyn.size(), f.geo.data(), (unsigned)f.geo.size(), 5, 0, out));
	EXPECT_FALSE(ComputeModelCorner(f.dyn.data(), (unsigned)f.dyn.size(), f.geo.data(), (unsigned)f.geo.size(), 0x80000005u, 0, out));
	EXPECT_FALSE(ComputeModelCorner(f.dyn.data(), (unsigned)f.dyn.size(), f.geo.data(), (unsigned)f.geo.size(), 0xC0000005u, 0, out));
}

// [Doruk-Aksoy] A record past the stream or an index past the geometry reads the last word rather than past either buffer, and an empty buffer reads zeros.
TEST(ModelGpu, ReadsStayInsideTheirBuffers)
{
	Frame f = Lay(true, 0, 4, 0.5f, 1, kIdentity, 0);
	float out[5];
	EXPECT_TRUE(ComputeModelCorner(f.dyn.data(), (unsigned)f.dyn.size(), f.geo.data(), (unsigned)f.geo.size(), ComputeModelRef(100000), 0, out));
	EXPECT_TRUE(ComputeModelCorner(f.dyn.data(), (unsigned)f.dyn.size(), f.geo.data(), (unsigned)f.geo.size(), f.ref, 5000000, out));
	EXPECT_TRUE(ComputeModelCorner(nullptr, 0, nullptr, 0, f.ref, 0, out));
	for (unsigned c = 0; c < 5; c++) EXPECT_EQ(0.f, out[c]);
}

// [Doruk-Aksoy] A keyframe's box holds exactly its span's corners, indexed or in order; a bad span gives none.
TEST(ModelGpu, ASpansBoxHoldsItsCorners)
{
	float box[6];
	ASSERT_TRUE(ComputeModelSpanBox(kVerts, 8, kIndices, 7, 4, 1, 6, box));
	EXPECT_FLOAT_EQ(10.f, box[0]); EXPECT_FLOAT_EQ(0.f, box[1]); EXPECT_FLOAT_EQ(2.f, box[2]);
	EXPECT_FLOAT_EQ(11.f, box[3]); EXPECT_FLOAT_EQ(1.f, box[4]); EXPECT_FLOAT_EQ(3.f, box[5]);
	ASSERT_TRUE(ComputeModelSpanBox(kVerts, 8, nullptr, 0, 0, 1, 2, box));
	EXPECT_FLOAT_EQ(0.f, box[0]); EXPECT_FLOAT_EQ(1.f, box[3]); EXPECT_FLOAT_EQ(0.f, box[1]); EXPECT_FLOAT_EQ(1.f, box[4]);
	EXPECT_FALSE(ComputeModelSpanBox(nullptr, 8, nullptr, 0, 0, 0, 3, box));
	EXPECT_FALSE(ComputeModelSpanBox(kVerts, 8, nullptr, 0, 0, 0, 0, box));
	EXPECT_FALSE(ComputeModelSpanBox(kVerts, 8, kIndices, 7, 0, 5, 3, box));
	EXPECT_FALSE(ComputeModelSpanBox(kVerts, 8, kIndices, 7, 0, 8, 1, box));
	EXPECT_FALSE(ComputeModelSpanBox(kVerts, 8, kIndices, 7, 4, 0, 1, box));
	EXPECT_FALSE(ComputeModelSpanBox(kVerts, 8, nullptr, 0, 4, 2, 3, box));
}

// [Doruk-Aksoy] The world box under a pose holds every corner of every lerp between the two keyframes.
TEST(ModelGpu, TheWorldBoxHoldsEveryLerp)
{
	float a[6], b[6], world[6];
	ASSERT_TRUE(ComputeModelSpanBox(kVerts, 8, kIndices, 7, 0, 1, 6, a));
	ASSERT_TRUE(ComputeModelSpanBox(kVerts, 8, kIndices, 7, 4, 1, 6, b));
	ComputeModelBoxUnder(a, b, kPose, world);
	for (float inter : { 0.f, 0.3f, 0.7f, 1.f })
	{
		float corners[6 * 5];
		ASSERT_TRUE(ComputeModelSurfaceCorners(kVerts, 8, kIndices, 7, { 0, 4, inter, 1, 6 }, kPose, corners));
		for (unsigned i = 0; i < 6; i++)
			for (unsigned c = 0; c < 3; c++)
			{
				EXPECT_LE(world[c], corners[i * 5 + c] + 1e-4f);
				EXPECT_GE(world[3 + c], corners[i * 5 + c] - 1e-4f);
			}
	}
	// [Doruk-Aksoy] Under the identity the box is the two keyframes' union, exactly.
	ComputeModelBoxUnder(a, b, kIdentity, world);
	EXPECT_FLOAT_EQ(0.f, world[0]); EXPECT_FLOAT_EQ(0.f, world[2]);
	EXPECT_FLOAT_EQ(11.f, world[3]); EXPECT_FLOAT_EQ(3.f, world[5]);
}

// [Doruk-Aksoy] ComputeDynWordsBound counts every piece at a packed sprite's words, which holds a model record only while the record is no bigger.
TEST(ModelGpu, ARecordFitsInAPackedSpritesWords)
{
	EXPECT_LE(kModelRecordWords, (unsigned)zx::hwrender::kSpriteWords);
}

// [Doruk-Aksoy] Every condition alone keeps a surface on the CPU; a geometry not yet asked about or held goes.
TEST(ModelGpu, ASurfaceGoesToTheGpuOnlyWhenEverythingAllows)
{
	EXPECT_TRUE(ComputeGpuModelWanted(true, true, true, false, 0));
	EXPECT_TRUE(ComputeGpuModelWanted(true, true, true, false, 1));
	EXPECT_FALSE(ComputeGpuModelWanted(true, true, true, false, 2));
	EXPECT_FALSE(ComputeGpuModelWanted(false, true, true, false, 0));
	EXPECT_FALSE(ComputeGpuModelWanted(true, false, true, false, 0));
	EXPECT_FALSE(ComputeGpuModelWanted(true, true, false, false, 0));
	EXPECT_FALSE(ComputeGpuModelWanted(true, true, true, true, 0));
}

// [Doruk-Aksoy] The buffer grows by half again from a 1 M word floor, never past what the device binds, and refuses what does not fit at all.
TEST(ModelGpu, TheResidentBufferGrowsWithinTheDevice)
{
	EXPECT_EQ(1u << 20, ComputeModelGeoCapacity(0, 1ull << 25));
	EXPECT_EQ(3000000u, ComputeModelGeoCapacity(2000000, 1ull << 25));
	EXPECT_EQ(2500000u, ComputeModelGeoCapacity(2000000, 2500000));
	EXPECT_EQ(1000u, ComputeModelGeoCapacity(1000, 1000));
	EXPECT_EQ(0u, ComputeModelGeoCapacity(1001, 1000));
	EXPECT_EQ(0xFFFFFFFFu, ComputeModelGeoCapacity(0xF0000000ull, 1ull << 40));
}
