// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Moving UP and DOWN inside an axis of pills that has wrapped onto several lines.
//
// A wrapped axis is a grid, and it was being navigated as a list: left and right walked the offered
// order and up and down left the axis entirely. On a three-line axis that means nine presses to get
// from the first option to the last, and the two keys that point at the line below did nothing with
// it. The eye sees rows; the keyboard should agree.
//
// So up and down move to the nearest pill on the adjacent LINE, by horizontal position -- the answer
// a grid gives, and the one a player expects from anything laid out in rows. Left and right are left
// alone: they walk the written order, crossing line ends as they go, which is what makes every option
// reachable in sequence however the wrapping falls out.
//
// NEAREST BY CENTRE, not by index. Pills are different widths, so the nth pill on one line can sit
// nowhere near the nth on the next; matching by index would send the marker sideways across the panel
// while the player pressed a key that points straight down.
//
// The unit answers LEAVES when there is no line that way, and the caller takes that as "this key
// belongs to the region, not to the axis" -- which is how up off the first line still reaches the
// control above and down off the last still reaches the one below.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_PILLGRID_COMPUTE_H
#define ZX_PILLGRID_COMPUTE_H

#include <vector>

#include "features/server-browser/computation/wadlist_compute.h"

namespace zx
{

struct PillMove
{
	// True when the axis has no line in that direction. The index is then meaningless and the
	// caller moves focus out of the axis instead.
	bool leaves;

	int index;	// where it lands, when it does not leave

	PillMove() : leaves(true), index(0) {}
};

// `layout` is the wrap already computed for this axis -- the same one the draw uses, so the grid the
// keyboard walks is the grid on screen. `widths` and `gap` are what produced it, and are what place
// each pill along its line.
//
// `dir` is -1 for up and +1 for down. An index outside the layout, or a layout with one line or
// none, leaves: there is nothing above or below a single row.
PillMove MovePillVertically(const WadListLayout &layout, const std::vector<int> &widths,
                            int gap, int index, int dir);

} // namespace zx

#endif // ZX_PILLGRID_COMPUTE_H
