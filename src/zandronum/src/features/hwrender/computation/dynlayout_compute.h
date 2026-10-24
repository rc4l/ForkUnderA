// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a frame's dynamic pieces go in the dynamic stream, among its packed sprites or general vertices, and behind the level's piece records, worked out before they are written so the writing can be split across lanes.

#ifndef ZX_HWRENDER_DYNLAYOUT_COMPUTE_H
#define ZX_HWRENDER_DYNLAYOUT_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] One dynamic piece's place: the piece, its first vertex of the stream and how many; a packed sprite's index among the frame's, else -1 with its first general vertex and its record behind the level's, -1 when the records ran out.
struct DynSlot { int piece; unsigned first, count; int record; int sprite; unsigned general; };

// [rc4l] What a layout came to: the stream's vertices, the general vertices among them and the packed sprites.
struct DynLayoutTotals { unsigned stream, general; int sprites; };

// [rc4l] The pieces in draw order (`order`, `count` of them) back to back, `vertices[piece]` each and those with none left out; a packed piece (`packed[piece]`) takes the next sprite, any other the next general vertices and a record while `recordsLeft` last.
DynLayoutTotals ComputeDynLayout(const int *order, int count, const unsigned *vertices, const bool *packed, int recordsLeft, std::vector<DynSlot> &out);

// [rc4l] The most dynamic words a frame of `pieces` pieces over `vertices` engine vertices can take: every piece packed (kSpriteWords) and every vertex general, a quad's four corners as six, plus the alignment before the sprites.
unsigned ComputeDynWordsBound(unsigned pieces, unsigned vertices);

// [rc4l] The dynamic words to make room for when `need` are wanted: half as many again, and never under 1 M words (4 MB), which a Complex Doom fight or nuts.wad's horde stays inside, since growing it makes the pipelines binding it again.
unsigned ComputeDynWordsCapacity(unsigned need);

}} // namespace zx::hwrender

#endif // ZX_HWRENDER_DYNLAYOUT_COMPUTE_H
