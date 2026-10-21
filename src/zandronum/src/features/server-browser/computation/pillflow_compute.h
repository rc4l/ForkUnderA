// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Laying a row of pills out left to right and wrapping them, and saying how tall that came
// out.
//
// A list of short labels down a column wastes most of its width: "doom2.wad" is a third of the box
// it sits in, and twenty-five of them is twenty-five rows of mostly nothing. Side by side, the same
// twenty-five fit in a handful of rows.
//
// The HEIGHT IS THE POINT of this being computed rather than drawn directly. Wrapping means the
// content height depends on the labels, so whether it fits inside its panel is not something the
// author can know while writing the panel -- it changes with the player's files. So the layout is
// measured first and the caller is told how many rows it came to, which is what lets it scroll when
// there are more than it can show instead of drawing them off the bottom edge.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_PILLFLOW_COMPUTE_H
#define ZX_PILLFLOW_COMPUTE_H

#include <vector>

namespace zx
{

struct PillPlace
{
	int x;			// offset from the content's left edge
	int row;		// 0-based
	int width;

	PillPlace() : x(0), row(0), width(0) {}
	PillPlace(int px, int prow, int pw) : x(px), row(prow), width(pw) {}
};

// Where each pill goes, given its width. `gap` sits between neighbours on a row.
//
// A pill wider than the whole content width still gets a row of its own rather than being dropped
// or shrunk: a name too long to fit is a name the player still has to be able to pick, and the
// caller can clip the text where it draws it.
std::vector<PillPlace> FlowPills(const std::vector<int> &widths, int contentWidth, int gap);

// How many rows a placement came to. Zero for an empty one, which is what a caller sizing a box
// around it needs rather than a special case.
int PillFlowRowCount(const std::vector<PillPlace> &placed);

// Which pill a point lands on, or -1. Takes the same placement the draw used, so a pill cannot be
// clickable anywhere other than where it was drawn.
int PillFlowHitTest(const std::vector<PillPlace> &placed, int rowHeight, int x, int y);

} // namespace zx

#endif // ZX_PILLFLOW_COMPUTE_H
