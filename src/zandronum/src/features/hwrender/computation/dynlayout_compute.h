// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a frame's dynamic pieces go in the dynamic vertex buffer and behind the level's piece records, worked out before they are written so the writing can be split across lanes.

#ifndef ZX_HWRENDER_DYNLAYOUT_COMPUTE_H
#define ZX_HWRENDER_DYNLAYOUT_COMPUTE_H

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] One dynamic piece's place: the piece, its first vertex and vertex count, and its record behind the level's, or -1 when the records ran out.
struct DynSlot { int piece; unsigned first, count; int record; };

// [rc4l] The pieces in draw order (`order`, `count` of them) back to back, `vertices[piece]` each, those with none left out, and a record each while `recordsLeft` last; returns the vertex total.
unsigned ComputeDynLayout(const int *order, int count, const unsigned *vertices, int recordsLeft, std::vector<DynSlot> &out);

}} // namespace zx::hwrender

#endif // ZX_HWRENDER_DYNLAYOUT_COMPUTE_H
