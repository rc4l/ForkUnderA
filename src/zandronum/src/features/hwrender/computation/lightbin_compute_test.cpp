// [Doruk-Aksoy] Tests for lightbin_compute: the banded binning against the single pass BuildLightClusters made before it.
#include <gtest/gtest.h>

#include <vector>

#include "features/hwrender/computation/lightbin_compute.h"

using namespace zx::hwrender;

namespace {

ClusterGrid Grid(int tilesX, int tilesY, int slices)
{
	ClusterGrid g = ComputeGridForScreen(320, 200, kClusterZNear, kClusterZFar);
	g.tilesX = tilesX; g.tilesY = tilesY; g.slices = slices;
	return g;
}

ClusterRange Range(int x0, int x1, int y0, int y1, int z0, int z1)
{
	const ClusterRange r = { x0, x1, y0, y1, z0, z1, false };
	return r;
}

struct Bins
{
	std::vector<unsigned> counts, offsets, indices;
	unsigned total;
	int truncated;
};

// [Doruk-Aksoy] BuildLightClusters' binning before the bands: ComputeClusterIndex for every cell, every light in one pass.
Bins Reference(const ClusterGrid &g, const std::vector<ClusterRange> &ranges, unsigned perCell, unsigned capacity)
{
	const int cells = ComputeClusterCount(g);
	Bins b;
	b.counts.assign(cells, 0); b.offsets.assign(cells, 0); b.truncated = 0;
	for (const ClusterRange &r : ranges)
	{
		if (r.empty) continue;
		for (int z = r.z0; z <= r.z1; z++) for (int y = r.y0; y <= r.y1; y++) for (int x = r.x0; x <= r.x1; x++)
			b.counts[ComputeClusterIndex(g, x, y, z)]++;
	}
	for (int i = 0; i < cells; i++) if (b.counts[i] > perCell) { b.counts[i] = perCell; b.truncated++; }
	b.total = 0;
	for (int i = 0; i < cells; i++)
	{
		b.offsets[i] = b.total;
		if (b.total + b.counts[i] > capacity) { b.counts[i] = capacity > b.total ? capacity - b.total : 0; b.truncated++; }
		b.total += b.counts[i];
	}
	std::vector<unsigned> filled(cells, 0);
	b.indices.assign(b.total > 0 ? b.total : 1, 0);
	for (size_t l = 0; l < ranges.size(); l++)
	{
		const ClusterRange &r = ranges[l];
		if (r.empty) continue;
		for (int z = r.z0; z <= r.z1; z++) for (int y = r.y0; y <= r.y1; y++) for (int x = r.x0; x <= r.x1; x++)
		{
			const int cell = ComputeClusterIndex(g, x, y, z);
			if (filled[cell] >= b.counts[cell]) continue;
			b.indices[b.offsets[cell] + filled[cell]++] = (unsigned)l;
		}
	}
	return b;
}

// [Doruk-Aksoy] The bands' way: each band of the split counted, then filled, in the order given.
Bins Banded(const ClusterGrid &g, const std::vector<ClusterRange> &ranges, unsigned perCell, unsigned capacity, int rowGroups, bool reverse)
{
	const int cells = ComputeClusterCount(g);
	const int bands = ComputeClusterBandCount(g, rowGroups);
	Bins b;
	b.counts.assign(cells, 0); b.offsets.assign(cells, 0); b.truncated = 0;
	for (int k = 0; k < bands; k++)
		ComputeLightBinCounts(g, ranges.data(), (int)ranges.size(), ComputeClusterBand(g, reverse ? bands - 1 - k : k, rowGroups), b.counts.data());
	b.total = ComputeLightBinOffsets(b.counts.data(), cells, perCell, capacity, b.offsets.data(), b.truncated);
	std::vector<unsigned> filled(cells, 0);
	b.indices.assign(b.total > 0 ? b.total : 1, 0);
	for (int k = 0; k < bands; k++)
		ComputeLightBinFill(g, ranges.data(), (int)ranges.size(), ComputeClusterBand(g, reverse ? bands - 1 - k : k, rowGroups),
			b.counts.data(), b.offsets.data(), filled.data(), b.indices.data());
	return b;
}

// [Doruk-Aksoy] Ranges inside the grid of every size, one in seven empty, from a fixed seed.
std::vector<ClusterRange> RandomRanges(const ClusterGrid &g, int n, unsigned seed)
{
	std::vector<ClusterRange> out;
	auto next = [&seed](int span) { seed = seed * 1664525u + 1013904223u; return (int)((seed >> 8) % (unsigned)span); };
	for (int i = 0; i < n; i++)
	{
		const int x0 = next(g.tilesX), y0 = next(g.tilesY), z0 = next(g.slices);
		ClusterRange r = Range(x0, x0 + next(g.tilesX - x0), y0, y0 + next(g.tilesY - y0), z0, z0 + next(g.slices - z0));
		r.empty = next(7) == 0;
		out.push_back(r);
	}
	return out;
}

void ExpectSame(const Bins &a, const Bins &b)
{
	EXPECT_EQ(a.counts, b.counts);
	EXPECT_EQ(a.offsets, b.offsets);
	EXPECT_EQ(a.indices, b.indices);
	EXPECT_EQ(a.total, b.total);
	EXPECT_EQ(a.truncated, b.truncated);
}

} // namespace

// [Doruk-Aksoy] Any split, in any order, gives every cell the lights one pass gave it, in the same order, ceilings and all.
TEST(LightBin, BandsBinEveryLightAsOnePassDid)
{
	const ClusterGrid g = Grid(kClusterTilesX, kClusterTilesY, kClusterSlices);
	const std::vector<ClusterRange> ranges = RandomRanges(g, 400, 7u);
	const unsigned limits[][2] = { { 256u, 1u << 20 }, { 8u, 1u << 20 }, { 256u, 30000u } };
	for (const auto &lim : limits)
	{
		const Bins ref = Reference(g, ranges, lim[0], lim[1]);
		EXPECT_GT(ref.total, 0u);
		for (int rowGroups : { 1, 3, (int)kClusterTilesY, 40 })
			for (bool reverse : { false, true })
			{
				SCOPED_TRACE(testing::Message() << "perCell " << lim[0] << " capacity " << lim[1] << " rowGroups " << rowGroups << (reverse ? " reversed" : ""));
				ExpectSame(ref, Banded(g, ranges, lim[0], lim[1], rowGroups, reverse));
			}
	}
	EXPECT_GT(Reference(g, ranges, 8u, 1u << 20).truncated, 0) << "the small ceiling is reached";
	EXPECT_GT(Reference(g, ranges, 256u, 30000u).truncated, 0) << "the small buffer is reached";
}

// [Doruk-Aksoy] Past a cell's ceiling the cell keeps its first lights and counts once as truncated.
TEST(LightBin, ACellPastItsCeilingKeepsItsFirstLights)
{
	const ClusterGrid g = Grid(2, 2, 2);
	const std::vector<ClusterRange> ranges = { Range(1, 1, 0, 0, 1, 1), Range(1, 1, 0, 0, 1, 1), Range(0, 1, 0, 0, 1, 1), Range(1, 1, 0, 0, 1, 1) };
	const Bins b = Banded(g, ranges, 2u, 100u, 2, false);
	const int cell = ComputeClusterIndex(g, 1, 0, 1);
	EXPECT_EQ(2u, b.counts[cell]);
	EXPECT_EQ(0u, b.indices[b.offsets[cell]]);
	EXPECT_EQ(1u, b.indices[b.offsets[cell] + 1]);
	EXPECT_EQ(1, b.truncated);
	EXPECT_EQ(3u, b.total);
}

// [Doruk-Aksoy] The cell where the buffer runs out keeps what fits, and the cells after it are emptied, each counted.
TEST(LightBin, TheBufferRunningOutCutsCellsInOrder)
{
	unsigned counts[] = { 2, 3, 2, 1 }, offsets[4] = { 9, 9, 9, 9 };
	int truncated = 0;
	EXPECT_EQ(4u, ComputeLightBinOffsets(counts, 4, 256u, 4u, offsets, truncated));
	EXPECT_EQ(2u, counts[0]); EXPECT_EQ(2u, counts[1]); EXPECT_EQ(0u, counts[2]); EXPECT_EQ(0u, counts[3]);
	EXPECT_EQ(0u, offsets[0]); EXPECT_EQ(2u, offsets[1]); EXPECT_EQ(4u, offsets[2]); EXPECT_EQ(4u, offsets[3]);
	EXPECT_EQ(3, truncated);
}

// [Doruk-Aksoy] An empty range lands nowhere.
TEST(LightBin, AnEmptyRangeBinsNothing)
{
	const ClusterGrid g = Grid(3, 3, 3);
	ClusterRange r = Range(0, 2, 0, 2, 0, 2);
	r.empty = true;
	std::vector<unsigned> counts(ComputeClusterCount(g), 0);
	ComputeLightBinCounts(g, &r, 1, ComputeWholeGridBand(g), counts.data());
	for (unsigned c : counts) EXPECT_EQ(0u, c);
}

// [Doruk-Aksoy] A range reaching past the grid on every side covers the grid's cells once each and writes nothing outside it.
TEST(LightBin, ARangePastTheGridIsCutToIt)
{
	const ClusterGrid g = Grid(3, 2, 2);
	const ClusterRange r = Range(-4, 9, -1, 5, -2, 7);
	std::vector<unsigned> counts(ComputeClusterCount(g) + 1, 0);
	ComputeLightBinCounts(g, &r, 1, ComputeWholeGridBand(g), counts.data());
	for (int i = 0; i < ComputeClusterCount(g); i++) EXPECT_EQ(1u, counts[i]) << i;
	EXPECT_EQ(0u, counts.back());
}

// [Doruk-Aksoy] However many groups are asked for, the bands cover each slice's rows once, with at least one and at most one a row.
TEST(LightBin, BandsCoverEachCellOnce)
{
	const ClusterGrid g = Grid(4, 5, 3);
	for (int rowGroups : { 0, 1, 2, 5, 9 })
	{
		const int groups = rowGroups < 1 ? 1 : rowGroups > 5 ? 5 : rowGroups;
		ASSERT_EQ(3 * groups, ComputeClusterBandCount(g, rowGroups));
		std::vector<int> seen(3 * 5, 0);
		for (int k = 0; k < ComputeClusterBandCount(g, rowGroups); k++)
		{
			const ClusterBand b = ComputeClusterBand(g, k, rowGroups);
			EXPECT_EQ(b.zBegin + 1, b.zEnd);
			for (int y = b.yBegin; y < b.yEnd; y++) seen[b.zBegin * 5 + y]++;
		}
		for (int s : seen) EXPECT_EQ(1, s) << rowGroups;
	}
	const ClusterBand whole = ComputeWholeGridBand(g);
	EXPECT_EQ(0, whole.zBegin); EXPECT_EQ(3, whole.zEnd); EXPECT_EQ(0, whole.yBegin); EXPECT_EQ(5, whole.yEnd);
}

// [Doruk-Aksoy] A grid with no cells has no bands, and asking for one of its bands divides by nothing.
TEST(LightBin, AGridWithNoCellsHasNoBands)
{
	EXPECT_EQ(0, ComputeClusterBandCount(Grid(0, 4, 3), 2));
	const ClusterBand b = ComputeClusterBand(Grid(4, 0, 3), 1, 2);
	EXPECT_EQ(1, b.zBegin);
	EXPECT_EQ(0, b.yBegin); EXPECT_EQ(0, b.yEnd);
}
