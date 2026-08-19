// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether a bulk change to the map rotation would actually change anything.
//
// SELECT ALL and DESELECT ALL are confirmed, because either can undo a rotation somebody spent time
// curating and there is no undo. But a confirmation for a press that would do nothing is a question
// with one honest answer, and asking it teaches people to dismiss the box without reading -- which
// is exactly the habit that makes the confirmation worthless on the press that does matter.
//
// So the question is asked of the LIST, not of the button: select-all on a rotation that already has
// every map in it is a no-op, and so is deselect-all on one that has none, however they got that way.
// Reordering does not count as a difference, because neither button reorders.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_MAPSELECT_COMPUTE_H
#define ZX_MAPSELECT_COMPUTE_H

namespace zx
{

// [rc4l] `inCount` is how many maps are currently in the rotation, `total` how many exist to choose
// from. Both bulk actions are no-ops on an empty list -- there is nothing to put in or take out.
bool ComputeSelectAllChanges(int inCount, int total);
bool ComputeDeselectAllChanges(int inCount, int total);

} // namespace zx

#endif // ZX_MAPSELECT_COMPUTE_H
