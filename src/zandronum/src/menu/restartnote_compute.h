// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] When a menu option that only takes effect after a restart says so beside its value, rather than on a line of its own.

#ifndef ZX_RESTARTNOTE_COMPUTE_H
#define ZX_RESTARTNOTE_COMPUTE_H

namespace zx {

// [rc4l] Which wording an option row's note uses and the scale it is drawn at, wording -1 when it shows none.
struct RestartNoteFit
{
	int wording;
	int scale;
};

// [rc4l] No note until value (as the option's cvar prints it) differs from launched, the value the game started with, and never for an unrecorded launch value or a greyed row.
// [rc4l] Then the largest scale from the menu's own down to 1x at which a gap and a wording fit between valueRight and rightEdge, trying the wordings longest first; gap and widths are in the font's pixels.
RestartNoteFit ComputeRestartNote(const char *value, const char *launched, bool recorded, bool grayed,
	int valueRight, int gap, int rightEdge, int scale, const int *widths, int count);

} // namespace zx

#endif // ZX_RESTARTNOTE_COMPUTE_H
