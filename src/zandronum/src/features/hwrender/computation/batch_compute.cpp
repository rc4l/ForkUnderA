// [rc4l] Implementation of draw-run coalescing. No engine deps.
#include "features/hwrender/computation/batch_compute.h"

#include <algorithm>

namespace hwrender
{

bool ComputeStaysInBatch(const void *pieceMaterial, const void *pieceBase, const void *batchMaterial, const void *batchBase)
{
	return pieceMaterial == batchMaterial || (pieceBase != nullptr && pieceBase == batchBase);
}

RebatchPlan ComputeRebatchPlan(int pieces, bool overflowed, int translucent)
{
	if (overflowed) return RebatchPlan::Rebuild;
	if (pieces <= 0) return RebatchPlan::None;
	return translucent > 0 ? RebatchPlan::Rebuild : RebatchPlan::Append;
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

void ComputeGroupByKey(const uint64_t *keys, int count, std::vector<int> &order, std::vector<BatchRun> &groups)
{
	order.resize(count);
	for (int i = 0; i < count; i++) order[i] = i;
	std::sort(order.begin(), order.end(), [keys](int a, int b) { return keys[a] != keys[b] ? keys[a] < keys[b] : a < b; });
	std::vector<uint64_t> sorted(count);
	for (int i = 0; i < count; i++) sorted[i] = keys[order[i]];
	groups.resize(count);
	groups.resize(count > 0 ? ComputeBatchRuns(&sorted[0], count, &groups[0], count) : 0);
}

} // namespace hwrender
