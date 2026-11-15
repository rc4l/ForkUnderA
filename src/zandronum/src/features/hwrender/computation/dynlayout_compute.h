// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a frame's dynamic pieces go in the dynamic stream, among its packed sprites or general vertices, and behind the level's piece records, worked out before they are written so the writing can be split across lanes.

#ifndef ZX_HWRENDER_DYNLAYOUT_COMPUTE_H
#define ZX_HWRENDER_DYNLAYOUT_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] One dynamic piece's place: the piece, its first vertex of the stream and how many; a packed sprite's index among the frame's, else -1 with its first general vertex and its record behind the level's, -1 when the records ran out.
// [Doruk-Aksoy] `model` is a GPU model surface's index among the frame's model records, -1 for any other piece.
struct DynSlot { int piece; unsigned first, count; int record; int sprite; unsigned general; int model; };

// [rc4l] What a layout came to: the stream's vertices, the general vertices among them and the packed sprites.
// [Doruk-Aksoy] ...and the GPU model surfaces.
struct DynLayoutTotals { unsigned stream, general; int sprites; int models; };

// [rc4l] The pieces in draw order (`order`, `count` of them) back to back, `vertices[piece]` each and those with none left out; a packed piece (`packed[piece]`) takes the next sprite, any other the next general vertices and a record while `recordsLeft` last.
// [Doruk-Aksoy] A GPU model surface (`model[piece]`, none when model is NULL) takes the next model record and a record, but no general vertices, since its corners come from the resident geometry.
DynLayoutTotals ComputeDynLayout(const int *order, int count, const unsigned *vertices, const bool *packed, int recordsLeft, std::vector<DynSlot> &out,
	const bool *model = nullptr);

// [rc4l] The most dynamic words a frame of `pieces` pieces over `vertices` engine vertices can take: every piece packed (kSpriteWords) and every vertex general, a quad's four corners as six, plus the alignment before the sprites.
// [Doruk-Aksoy] ...and its `matrices` model matrices behind the sprites.
// [Doruk-Aksoy] A GPU model surface's record (kModelRecordWords) is under a packed sprite's words, so the bound holds it too.
unsigned ComputeDynWordsBound(unsigned pieces, unsigned vertices, unsigned matrices);

// [Doruk-Aksoy] Words a model matrix takes in the dynamic stream, its columns one after another.
const unsigned kModelMatrixWords = 16;

// [Doruk-Aksoy] What a piece record carries for its model matrix, the `index`th of the frame's from word `matBase`: that word plus one, so 0 says it has none (index -1).
float ComputeModelMatrixWord(unsigned matBase, int index);

// [rc4l] The dynamic words to make room for when `need` are wanted: half as many again, and never under 1 M words (4 MB), which a Complex Doom fight or nuts.wad's horde stays inside, since growing it makes the pipelines binding it again.
unsigned ComputeDynWordsCapacity(unsigned need);

// [Doruk-Aksoy] The storage buffer every Vulkan device can bind (maxStorageBufferRange's guaranteed least, 128 MB), which the dynamic stream assumes until the device says more.
const unsigned long long kMinStorageBufferBytes = 1ull << 27;

// [Doruk-Aksoy] The most engine vertices the dynamic stream takes in a frame, never past this, which keeps its word sums inside 32 bits.
const unsigned kDynVertexLimit = 100000000u;

// [Doruk-Aksoy] The most engine vertices a frame's dynamic stream may hold when the device binds storage buffers of up to `maxStorageBytes`: the most whose words, at the capacity ComputeDynWordsCapacity grows to and with a piece and a model matrix every three vertices, still fit; 0 when not even the smallest capacity does.
unsigned ComputeDynVertexCeiling(unsigned long long maxStorageBytes);

}} // namespace zx::hwrender

#endif // ZX_HWRENDER_DYNLAYOUT_COMPUTE_H
