// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The rows of the experience list, once an entry can be opened to show its ways of playing.
//
// The list used to be one row per entry, and the row index WAS the entry index: the two could not
// drift apart because they were the same number. Opening an entry breaks that, and the failure mode
// is nasty -- the panel describes one experience while the button starts another, and both look
// right on their own. So the flattening is done here, once, and the drawing, the mouse and the
// keyboard all read the same list.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_HOSTLIST_COMPUTE_H
#define ZX_HOSTLIST_COMPUTE_H

#include <vector>

namespace zx
{

struct HostListRow
{
	// Which catalogue entry this row belongs to. Always valid: a variant row belongs to the entry it
	// hangs under, which is what lets a click on either kind answer "what would this host".
	int entry;

	// -1 for the entry's own row, otherwise the index of one of its variants. The sign is the whole
	// distinction, so callers never have to ask the catalogue what kind of row they are looking at.
	int variant;

	HostListRow() : entry(0), variant(-1) {}
	HostListRow(int e, int v) : entry(e), variant(v) {}
};

// `variantCounts` is how many ways each entry can be played, in catalogue order. `openEntry` is the
// one whose ways are showing, or -1 for none.
//
// An entry with no variants is never opened, however hard the caller asks: there is nothing to show,
// and a caret on a row that cannot open is a promise the list does not keep.
std::vector<HostListRow> BuildHostListRows(const std::vector<int> &variantCounts, int openEntry);

// Which row is the cursor, given what is chosen. Answers -1 when the selection names something the
// list does not contain, which happens the moment a catalogue is re-read underneath it.
//
// `variant` of -1 asks for the entry's own row; a variant that is not showing (because its entry is
// shut) falls back to that same row, so a selection is never invisible.
int FindHostListRow(const std::vector<HostListRow> &rows, int entry, int variant);

} // namespace zx

#endif // ZX_HOSTLIST_COMPUTE_H
