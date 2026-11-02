// [Doruk-Aksoy] A frame's lights binned into the cluster grid's flat runs a band of cells at a time, so bands on separate lanes give each cell its lights in light order as one pass would.
#ifndef ZX_LIGHTBIN_COMPUTE_H
#define ZX_LIGHTBIN_COMPUTE_H

#include "features/hwrender/computation/lightcluster_compute.h"

namespace zx { namespace hwrender {

// [Doruk-Aksoy] Depth slices [zBegin, zEnd) and tile rows [yBegin, yEnd), a block of cells no other band of the same split shares.
struct ClusterBand
{
	int zBegin, zEnd;
	int yBegin, yEnd;
};

// [Doruk-Aksoy] How many bands split each slice's rows into `rowGroups` groups, at least one and at most one a row; 0 for a grid with no cells.
int ComputeClusterBandCount(const ClusterGrid &g, int rowGroups);

// [Doruk-Aksoy] Band `index` of that split, slice by slice and down each slice's rows.
ClusterBand ComputeClusterBand(const ClusterGrid &g, int index, int rowGroups);

// [Doruk-Aksoy] Every slice and row of the grid as one band, for binning on one lane.
ClusterBand ComputeWholeGridBand(const ClusterGrid &g);

// [Doruk-Aksoy] Add one to counts[cell] for each light whose range covers a cell of `band`, a range reaching past the grid cut to it.
void ComputeLightBinCounts(const ClusterGrid &g, const ClusterRange *ranges, int lightCount, const ClusterBand &band, unsigned *counts);

// [Doruk-Aksoy] Cap each cell's count at perCell, then cut the cells whose runs would pass capacity, writing each run's start; returns the runs' total, every cell capped or cut added to truncated.
unsigned ComputeLightBinOffsets(unsigned *counts, int cells, unsigned perCell, unsigned capacity, unsigned *offsets, int &truncated);

// [Doruk-Aksoy] Write the index of each light covering a cell of `band` into that cell's run in light order, up to its count, filled[cell] (0 to begin with) counting what is written.
void ComputeLightBinFill(const ClusterGrid &g, const ClusterRange *ranges, int lightCount, const ClusterBand &band,
	const unsigned *counts, const unsigned *offsets, unsigned *filled, unsigned *indices);

// [Doruk-Aksoy] The cells [first, end) of `band` as ComputeClusterIndex numbers them, one run for a band of one slice or of every row.
void ComputeClusterBandCells(const ClusterGrid &g, const ClusterBand &band, int &first, int &end);

// [Doruk-Aksoy] One band's count pass: its cells zeroed, counted as ComputeLightBinCounts counts them and capped at perCell (each cap added to truncated); returns the band's total.
unsigned ComputeLightBinBandCounts(const ClusterGrid &g, const ClusterRange *ranges, int lightCount, const ClusterBand &band,
	unsigned perCell, unsigned *counts, int &truncated);

// [Doruk-Aksoy] Each band's first run start from the bands' totals in band order, their sum in total; false when it passes capacity, where ComputeLightBinOffsets must cut the runs as one pass does.
bool ComputeLightBinBandStarts(const unsigned *totals, int bands, unsigned capacity, unsigned *starts, unsigned &total);

// [Doruk-Aksoy] One band's fill pass: with layOut its runs laid from start in cell order first, then its cells' filled zeroed, its lights written as ComputeLightBinFill writes them, and each cell's (run start, count) pair put in table.
void ComputeLightBinBandFill(const ClusterGrid &g, const ClusterRange *ranges, int lightCount, const ClusterBand &band,
	bool layOut, unsigned start, const unsigned *counts, unsigned *offsets, unsigned *filled, unsigned *indices, unsigned *table);

}} // namespace zx::hwrender

#endif
