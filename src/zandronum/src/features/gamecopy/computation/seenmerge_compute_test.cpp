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

// [Doruk-Aksoy] A fill that compares the sectors and lines whole takes their bits from what it found changed, one the tic's end scanned from the scan, and bits of another map's sizes are read whole.
TEST(SeenMerge, EachFillTakesTheBitsFromWhatItHas)
{
	EXPECT_EQ(kSeenFromFill, ComputeSeenSource(false, true, false));
	EXPECT_EQ(kSeenFromFill, ComputeSeenSource(false, true, true)) << "a stale scan's subsectors are no use to an unscanned fill";
	EXPECT_EQ(kSeenFromScan, ComputeSeenSource(true, true, true));
	EXPECT_EQ(kSeenReadWhole, ComputeSeenSource(true, true, false)) << "a scan that read another map's subsectors";
	EXPECT_EQ(kSeenReadWhole, ComputeSeenSource(false, false, true)) << "a new level's bits";
	EXPECT_EQ(kSeenReadWhole, ComputeSeenSource(true, false, true));
}

// [Doruk-Aksoy] Indices past either end change nothing.
TEST(SeenMerge, IndicesOutOfRangeAreIgnored)
{
	std::vector<unsigned char> bits = { 0, 0 };
	ComputeSeenMerge(bits, { { -1, 1 }, { 2, 1 } }, { -3, 5 });
	EXPECT_EQ((std::vector<unsigned char>{ 0, 0 }), bits);
}
