// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Per-cell arrays beside the blockmap's lists, so a position check reads candidates from contiguous memory; see README.md.
#ifndef ZX_CELLARRAYS_H
#define ZX_CELLARRAYS_H

#include "features/blockmap-filter/computation/cellarrays_compute.h"
#include <cstddef>
#include <cstdint>

class AActor;
struct FBlockNode;
struct line_t;
class FBoundingBox;

namespace zx { namespace cells {

// [Doruk-Aksoy] Bumped by every link and unlink in a cell, so a walk over the arrays knows when to hand over to the lists.
extern unsigned int Epoch;

// [Doruk-Aksoy] Empties the arrays for a blockmap of `count` cells, or frees them with 0.
void Reset(int count);

// [Doruk-Aksoy] Mirrors a node linked at the head of its cell's list.
void Link(FBlockNode *node, AActor *who, bool multi);

// [Doruk-Aksoy] Mirrors a node unlinked from its cell's list.
void Unlink(FBlockNode *node);

// [Doruk-Aksoy] Rewrites an actor's cached x, y and radii in every cell it is linked in, after it moved or resized without relinking.
void Refresh(AActor *who);

// [Doruk-Aksoy] A cell's things, head at the back.
const std::vector<ThingEntry> &Things(int cell);

// [Doruk-Aksoy] Builds the static line arrays not built yet, at the end of level setup so no later thread builds them.
void BuildStaticLines();

// [Doruk-Aksoy] Whether the sight arrays hold this level's lines, which they do unless a line lies past 32 bits.
bool SightReady();

// [Doruk-Aksoy] A cell's static lines as a sight trace tests them; read only when SightReady and the map has no polyobjects.
const std::vector<SightLineEntry> &SightLines(int cell);

// [Doruk-Aksoy] Whether a candidate is too far for PIT_CheckThing to act on, from its cached values.
bool Missed(const ThingEntry &e, const AActor *mover, int64_t moverX, int64_t moverY);

// [Doruk-Aksoy] The static lines near a box in FBlockLinesIterator's order, less those PIT_CheckLine's box test passes by; used only on maps without polyobjects.
class LineWalk
{
	int minx, maxx, miny, maxy, curx, cury;
	int64_t left, right, bottom, top;
	const std::vector<LineEntry> *cell;
	size_t pos;
	void Start(int x, int y);
public:
	explicit LineWalk(const FBoundingBox &box);
	line_t *Next();
};

}} // namespace zx::cells

#endif
