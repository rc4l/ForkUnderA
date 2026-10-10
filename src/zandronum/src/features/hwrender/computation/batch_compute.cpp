// [rc4l] Implementation of draw-run coalescing. No engine deps.
#include "features/hwrender/computation/batch_compute.h"

namespace hwrender
{

bool ComputeStaysInBatch(const void *pieceMaterial, const void *pieceBase, const void *batchMaterial, const void *batchBase)
{
	return pieceMaterial == batchMaterial || (pieceBase != nullptr && pieceBase == batchBase);
}

int ComputeBatchRuns(const uint64_t *keys, int count, BatchRun *outRuns, int maxRuns)
{
	int runs = 0;
	int i = 0;
	while (i < count)
	{
		int j = i + 1;
		while (j < count && keys[j] == keys[i])
			j++;

		if (runs < maxRuns)
			outRuns[runs] = BatchRun{i, j - i};
		runs++;
		i = j;
	}
	return runs < maxRuns ? runs : maxRuns;
}

} // namespace hwrender
