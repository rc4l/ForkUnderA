// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The size a grow-only GPU buffer is re-created at, for the per-frame streams (sprites, the 2D layer, sky, stacked and horizon surfaces).

#ifndef ZX_GROWBUFFER_COMPUTE_H
#define ZX_GROWBUFFER_COMPUTE_H

#include <cstddef>

namespace zx { namespace hwrender {

// [rc4l] The capacity a buffer short of needed elements takes: half again as many plus pad, so a frame that grows a little does not re-create it every time.
unsigned int ComputeGrownCapacity(unsigned int needed, unsigned int pad);

// [rc4l] Whether a grow-only buffer holds count elements: made, and its capacity at least count.
bool ComputeBufferFits(bool made, unsigned int capacity, unsigned int count);

// [rc4l] The fewest records a piece buffer keeps for a frame's dynamic pieces.
const unsigned int kDynamicRecordsAtLeast = 4096;

// [rc4l] The records a scene's piece buffer is made with: the level's own and half again for pieces appended in play, then half again over the most dynamic pieces a frame has had, kDynamicRecordsAtLeast at least.
unsigned int ComputePieceCapacity(unsigned int levelPieces, unsigned int dynamicPieces);

// [rc4l] Hands write(offset, size) each piece of at most chunk bytes a write of bytes is sent in (one piece when chunk is 0), so no upload needs a page bigger than chunk: a browser's device keeps every page it ever made.
template <class Write>
void ComputeUploadChunks(size_t bytes, size_t chunk, Write write)
{
	if (chunk == 0) chunk = bytes;
	for (size_t at = 0; at < bytes; at += chunk) write(at, bytes - at < chunk ? bytes - at : chunk);
}

// [rc4l] The bytes a browser's context takes into upload pages between flushes during a level's upload, half of Diligent's 8 MB WebGPU page, since every page one frame needs stays in its pool for good.
const size_t kUploadFlushBytes = 4u << 20;

// [rc4l] The flush budget while uploads are flushed: kUploadFlushBytes on a browser's device, none on another, nor before a device is made, as at a level's first upload, which made the window and the device itself.
size_t ComputeUploadFlushBudget(bool deviceMade, bool webgpu);

// [rc4l] Adds added to the bytes sent since the last flush and says whether one is due at budget (never at 0), counting from nothing again when it is.
bool ComputeUploadFlushDue(size_t &sinceFlush, size_t added, size_t budget);

}} // namespace zx::hwrender

#endif // ZX_GROWBUFFER_COMPUTE_H
