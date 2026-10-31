// [Doruk-Aksoy] Tests for seenmerge_compute: the last fill's bits, the changed ones' live bits over them, the frames' marks over those.
#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "features/gamecopy/computation/seenmerge_compute.h"

using namespace zx::gamecopy;

// [Doruk-Aksoy] A changed item takes its live bit, set or cleared, a marked one is seen whatever the tic did, and the rest keep the last fill's.
TEST(SeenMerge, ChangedTakeTheirLiveBitAndMarksWin)
{
	std::vector<unsigned char> bits = { 0, 1, 0, 1, 1 };
	ComputeSeenMerge(bits, { { 1, 0 }, { 2, 1 }, { 4, 0 } }, { 4 });
	EXPECT_EQ((std::vector<unsigned char>{ 0, 0, 1, 1, 1 }), bits);
}

// [Doruk-Aksoy] Indices past either end change nothing.
TEST(SeenMerge, IndicesOutOfRangeAreIgnored)
{
	std::vector<unsigned char> bits = { 0, 0 };
	ComputeSeenMerge(bits, { { -1, 1 }, { 2, 1 } }, { -3, 5 });
	EXPECT_EQ((std::vector<unsigned char>{ 0, 0 }), bits);
}
