// [rc4l] Pure draw-batching: collapse per-draw state keys into runs of identical adjacent state so the backend can merge consecutive same-state draws.
#ifndef ZX_HWRENDER_BATCH_COMPUTE_H
#define ZX_HWRENDER_BATCH_COMPUTE_H

#include <cstdint>

namespace hwrender
{

// [rc4l] A contiguous run of draws sharing one state key: [start, start+count).
struct BatchRun
{
	int start;
	int count;
};

// [rc4l] Group `count` consecutive state keys into runs of equal adjacent keys. Writes up to `maxRuns` runs into outRuns and returns the number written; if there are more runs than maxRuns the tail is dropped (callers should size maxRuns >= count, or check the return against expected).
int ComputeBatchRuns(const uint64_t *keys, int count, BatchRun *outRuns, int maxRuns);

// [rc4l] Whether a re-baked piece can stay in the batch it was built into: the same material, or another frame of the same base texture, which the batch re-resolves from that base every frame anyway; a switch changes the base too.
bool ComputeStaysInBatch(const void *pieceMaterial, const void *pieceBase, const void *batchMaterial, const void *batchBase);

} // namespace hwrender

#endif // ZX_HWRENDER_BATCH_COMPUTE_H
