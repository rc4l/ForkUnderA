// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] How the files an experience loads are laid out as running text.
//
// They used to be one per line with the size right-aligned beside each, which is a table. A table is
// the right shape for four files and the wrong one for twelve: it grows down the panel without
// bound, and everything under it -- what you play it with, what starts it -- got pushed off the
// bottom of a column that cannot scroll far enough to matter.
//
// So the names run on as a sentence, wrapped, and the sizes collapse into one total underneath. The
// list stops at a fixed number of lines when something needs the room below it, and runs as long as
// it likes when nothing does. Hovering says the rest.
//
// This unit takes WIDTHS, not text: measuring belongs to the font and the font belongs to the
// engine. The caller measures each name once and asks here where the breaks go.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_WADLIST_COMPUTE_H
#define ZX_WADLIST_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx
{

// One drawn line: the half-open range of files on it.
struct WadListLine
{
	size_t first;	// index of the first file on this line
	size_t end;		// one past the last

	WadListLine() : first(0), end(0) {}
	WadListLine(size_t f, size_t e) : first(f), end(e) {}
};

struct WadListLayout
{
	std::vector<WadListLine> lines;

	// How many files are actually drawn. Less than the total exactly when the cap bit.
	size_t shown;

	// Some files did not fit and the last line ends in an ellipsis. The caller draws the marker and
	// puts the whole list in a tooltip, because a list that silently stops is a list that lies.
	bool truncated;

	WadListLayout() : shown(0), truncated(false) {}
};

// `itemWidths` is the drawn width of each filename, in order.
// `sepWidth` is the width of ", " -- drawn after every file except the last one SHOWN.
// `ellipsisWidth` is the width of the marker appended when files are dropped.
// `maxWidth` is the room one line has.
// `maxLines` caps the number of lines; 0 means no cap, which is what an entry with nothing below the
// list gets.
//
// A file wider than a whole line still gets its own line rather than vanishing: the caller shortens
// the name, and a layout that dropped it would hide a file the player is about to download.
WadListLayout LayoutWadList(const std::vector<int> &itemWidths, int sepWidth,
                            int ellipsisWidth, int maxWidth, int maxLines);

} // namespace zx

#endif // ZX_WADLIST_COMPUTE_H
