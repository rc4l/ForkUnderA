// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure parts of the per-cell arrays: a cell's things in its list's order and a cell's static lines.
#ifndef ZX_CELLARRAYS_COMPUTE_H
#define ZX_CELLARRAYS_COMPUTE_H

#include <cstdint>
#include "features/fixed64/computation/fixed64_scale_compute.h"
#include <vector>

namespace zx { namespace cells {

// [Doruk-Aksoy] One actor's node in a cell: what PIT_CheckThing's distance test reads, the actor and node, and whether the actor has nodes in other cells too.
struct ThingEntry
{
	int64_t x, y, radius, attackRadius;
	void *actor;
	void *node;
	bool multi;
};

// [Doruk-Aksoy] A static line in a cell: its bounding box and the line.
struct LineEntry
{
	int64_t left, right, bottom, top;
	void *line;
};

// [Doruk-Aksoy] A static line in a cell as a sight trace tests it: its raw ends in v1, v2 order and its index, 20 bytes so a cell's lines share few cache lines.
struct SightLineEntry
{
	int32_t x1, y1, x2, y2;
	int32_t line;
};

// [Doruk-Aksoy] Links an entry at the head of a cell's list, which the array keeps at its back, so the list's order is the array's read backwards.
void ComputeCellLink(std::vector<ThingEntry> &cell, const ThingEntry &e);

// [Doruk-Aksoy] Unlinks a node's entry, keeping the others in order; false when the cell has none.
bool ComputeCellUnlink(std::vector<ThingEntry> &cell, const void *node);

// [Doruk-Aksoy] Rewrites the cached x, y and radii of a node's entry in place, never moving it; false when the cell has none.
bool ComputeCellRefresh(std::vector<ThingEntry> &cell, const void *node, int64_t x, int64_t y, int64_t radius, int64_t attackRadius);

// [Doruk-Aksoy] PIT_CheckLine's first test: whether a line's box misses the mover's box, so the line cannot touch it.
bool ComputeBoxMissesLine(const LineEntry &l, int64_t left, int64_t right, int64_t bottom, int64_t top);

// [Doruk-Aksoy] Whether a line's raw ends fit a SightLineEntry and its dx and dy are its ends' difference, so the entry gives P_SightCheckLine's answers exactly.
bool ComputeSightLineFits(int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t dx, int64_t dy);

// [Doruk-Aksoy] P_PointOnDivlineSide on raw values: 1 when (x, y) is right of the divline from (lx, ly) along (ldx, ldy).
inline int ComputeDivlineSide(int64_t x, int64_t y, int64_t lx, int64_t ly, int64_t ldx, int64_t ldy)
{
	return zx::DMulScale64(y - ly, ldx, lx - x, ldy, 32) > 0;
}

// [Doruk-Aksoy] P_SightCheckLine's two tests: whether the line's ends lie on both sides of the trace and the trace's ends on both sides of the line; inline, as the sight walk runs it for every line it meets.
inline bool ComputeSightLineCrossed(const SightLineEntry &l, int64_t tx, int64_t ty, int64_t tdx, int64_t tdy)
{
	if (ComputeDivlineSide(l.x1, l.y1, tx, ty, tdx, tdy) == ComputeDivlineSide(l.x2, l.y2, tx, ty, tdx, tdy)) return false;
	const int64_t dx = (int64_t)l.x2 - l.x1, dy = (int64_t)l.y2 - l.y1;
	return ComputeDivlineSide(tx, ty, l.x1, l.y1, dx, dy) != ComputeDivlineSide(tx + tdx, ty + tdy, l.x1, l.y1, dx, dy);
}

}} // namespace zx::cells

#endif
