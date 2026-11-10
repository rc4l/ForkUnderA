// [Doruk-Aksoy] Tests for dynquads_compute: a run of pieces placed where appending them one at a time would have put them.
#include <gtest/gtest.h>

#include "features/levelmesh/computation/dynquads_compute.h"

using namespace zx::levelmesh;

// [Doruk-Aksoy] A run starts at the stream's end.
TEST(DynQuads, ARunFollowsTheStream)
{
	DynQuadRun run = { 0, 0 };
	ASSERT_TRUE(ComputeDynRun(40, 7, 12, 300000, run));
	EXPECT_EQ(7u, run.firstPiece);
	EXPECT_EQ(40u, run.firstVert);
}

// [Doruk-Aksoy] A run that just fits the ceiling is placed, one vertex more is not, and the run is left as it was.
TEST(DynQuads, TheCeilingHoldsAsOneAppendAtATimeHeldIt)
{
	DynQuadRun run = { 99, 99 };
	EXPECT_TRUE(ComputeDynRun(92, 3, 8, 100, run));
	run = { 99, 99 };
	EXPECT_FALSE(ComputeDynRun(92, 3, 9, 100, run));
	EXPECT_EQ(99u, run.firstPiece);
	EXPECT_FALSE(ComputeDynRun(101, 3, 0, 100, run)) << "a stream already past the ceiling takes nothing";
}

// [Doruk-Aksoy] An empty run fits anywhere up to the ceiling, and a huge one cannot wrap around it.
TEST(DynQuads, EmptyAndHugeRuns)
{
	DynQuadRun run = { 0, 0 };
	EXPECT_TRUE(ComputeDynRun(100, 0, 0, 100, run));
	EXPECT_FALSE(ComputeDynRun(10, 0, 0xFFFFFFFFu, 0x7FFFFFFFu, run));
}
