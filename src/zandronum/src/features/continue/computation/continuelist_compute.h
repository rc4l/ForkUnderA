// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where the keyboard cursor goes, in a list that can be fifty rows long.
//
// A unit of its own because this is the whole of the keyboard contract and it is the part that
// cannot be checked by looking: whether End lands on the last row is obvious, whether Page Down from
// the last row STAYS there rather than wrapping to the top is not, and both are things a player
// finds by accident.
//
// WRAPPING IS FOR THE ARROWS ONLY. Up from the first row is the fastest way to the last, and every
// list in this engine already does that. Page Down is not a way to travel to the top: it says "a
// screenful further down", and a screenful further down than the end is the end. A page key that
// wrapped would make holding it a loop through the whole list, which is precisely what somebody
// paging through fifty rows to find one is not asking for.
//
// The rest of the list -- which rows are on screen, where the thumb sits, where a click lands -- is
// already written and already tested in the server browser's computation units (ComputeRowWindow,
// ComputeRestoredScroll, ComputeThumbHeight, ComputeThumbTop, ComputeFirstFromPointer). This unit
// deliberately does not restate any of it.
//
// Header-pure by the features/ rules: no engine types, so the key arrives already named.

#ifndef ZX_CONTINUELIST_COMPUTE_H
#define ZX_CONTINUELIST_COMPUTE_H

namespace zx
{

enum class ContinueListKey
{
	Up,
	Down,
	PageUp,
	PageDown,
	Home,
	End,
};

// The row the cursor moves to. `visibleRows` is how many fit on screen, which is what a page means.
// An empty list answers 0 and a selection outside the list is pulled back in, so a caller never has
// to check the answer before using it as an index.
int StepContinueList(ContinueListKey key, int selected, int count, int visibleRows);

// How many rows fit in a list of this height. Never less than one: a viewport too short for a whole
// row still has to show the row the cursor is on, or the keyboard moves through a list that never
// appears to change.
int ComputeContinueVisibleRows(int listHeight, int rowHeight);

} // namespace zx

#endif // ZX_CONTINUELIST_COMPUTE_H
