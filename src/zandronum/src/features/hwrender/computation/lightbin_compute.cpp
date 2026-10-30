// [Doruk-Aksoy] See lightbin_compute.h.
#include "features/hwrender/computation/lightbin_compute.h"

namespace zx { namespace hwrender {

namespace {

int Max(int a, int b) { return a > b ? a : b; }
int Min(int a, int b) { return a < b ? a : b; }

// [Doruk-Aksoy] Each cell of r inside band and the grid, a row at a time from the row's first cell, numbered as ComputeClusterIndex numbers them.
template <class F> void ForEachCell(const ClusterGrid &g, const ClusterRange &r, const ClusterBand &band, F f)
{
	if (r.empty) return;
	const int z0 = Max(Max(r.z0, band.zBegin), 0), z1 = Min(Min(r.z1, band.zEnd - 1), g.slices - 1);
	const int y0 = Max(Max(r.y0, band.yBegin), 0), y1 = Min(Min(r.y1, band.yEnd - 1), g.tilesY - 1);
	const int x0 = Max(r.x0, 0), x1 = Min(r.x1, g.tilesX - 1);
	for (int z = z0; z <= z1; z++)
		for (int y = y0; y <= y1; y++)
		{
			const int row = (z * g.tilesY + y) * g.tilesX;
			for (int x = x0; x <= x1; x++) f(row + x);
		}
}

} // namespace

int ComputeClusterBandCount(const ClusterGrid &g, int rowGroups)
{
	if (ComputeClusterCount(g) <= 0) return 0;
	return g.slices * Min(Max(rowGroups, 1), g.tilesY);
}

ClusterBand ComputeClusterBand(const ClusterGrid &g, int index, int rowGroups)
{
	const int groups = Min(Max(rowGroups, 1), Max(g.tilesY, 1));
	const int group = index % groups;
	ClusterBand b;
	b.zBegin = index / groups;
	b.zEnd = b.zBegin + 1;
	b.yBegin = g.tilesY * group / groups;
	b.yEnd = g.tilesY * (group + 1) / groups;
	return b;
}

ClusterBand ComputeWholeGridBand(const ClusterGrid &g)
{
	const ClusterBand b = { 0, g.slices, 0, g.tilesY };
	return b;
}

void ComputeLightBinCounts(const ClusterGrid &g, const ClusterRange *ranges, int lightCount, const ClusterBand &band, unsigned *counts)
{
	for (int l = 0; l < lightCount; l++)
		ForEachCell(g, ranges[l], band, [counts](int cell) { counts[cell]++; });
}

unsigned ComputeLightBinOffsets(unsigned *counts, int cells, unsigned perCell, unsigned capacity, unsigned *offsets, int &truncated)
{
	for (int i = 0; i < cells; i++)
		if (counts[i] > perCell) { counts[i] = perCell; truncated++; }
	unsigned total = 0;
	for (int i = 0; i < cells; i++)
	{
		offsets[i] = total;
		if (total + counts[i] > capacity)
		{
			counts[i] = capacity > total ? capacity - total : 0;
			truncated++;
		}
		total += counts[i];
	}
	return total;
}

void ComputeLightBinFill(const ClusterGrid &g, const ClusterRange *ranges, int lightCount, const ClusterBand &band,
	const unsigned *counts, const unsigned *offsets, unsigned *filled, unsigned *indices)
{
	for (int l = 0; l < lightCount; l++)
		ForEachCell(g, ranges[l], band, [&](int cell)
		{
			if (filled[cell] >= counts[cell]) return;
			indices[offsets[cell] + filled[cell]] = (unsigned)l;
			filled[cell]++;
		});
}

}} // namespace zx::hwrender
