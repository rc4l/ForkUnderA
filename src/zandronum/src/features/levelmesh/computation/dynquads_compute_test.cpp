// [Doruk-Aksoy] Tests for dynquads_compute: a run of quads placed where appending them one at a time would have put them.
#include <gtest/gtest.h>

#include "features/levelmesh/computation/dynquads_compute.h"

using namespace zx::levelmesh;

// [Doruk-Aksoy] A run starts at the stream's end, and each quad's vertices follow the last one's.
TEST(DynQuads, ARunFollowsTheStream)
{
	DynQuadRun run = { 0, 0 };
	ASSERT_TRUE(ComputeDynQuadRun(40, 7, 3, kDynVertexCeiling, run));
	EXPECT_EQ(7u, run.firstPiece);
	EXPECT_EQ(40u, run.firstVert);
	EXPECT_EQ(40u, ComputeDynQuadVertex(run, 0));
	EXPECT_EQ(48u, ComputeDynQuadVertex(run, 2));
}

// [Doruk-Aksoy] A run that just fits the ceiling is placed, one quad more is not, and the run is left as it was.
TEST(DynQuads, TheCeilingHoldsAsOneAppendAtATimeHeldIt)
{
	DynQuadRun run = { 99, 99 };
	EXPECT_TRUE(ComputeDynQuadRun(92, 3, 2, 100, run));
	run = { 99, 99 };
	EXPECT_FALSE(ComputeDynQuadRun(92, 3, 3, 100, run));
	EXPECT_EQ(99u, run.firstPiece);
	EXPECT_FALSE(ComputeDynQuadRun(101, 3, 0, 100, run)) << "a stream already past the ceiling takes nothing";
}

// [Doruk-Aksoy] An empty run fits anywhere up to the ceiling, and a huge one cannot wrap around it.
TEST(DynQuads, EmptyAndHugeRuns)
{
	DynQuadRun run = { 0, 0 };
	EXPECT_TRUE(ComputeDynQuadRun(100, 0, 0, 100, run));
	EXPECT_FALSE(ComputeDynQuadRun(0, 0, 0x40000001u, kDynVertexCeiling, run));
}
