// [rc4l] Pure geometry/color math for a menu prompt's background panel (rounded corners + vertical
// gradient), so a dialog's text stays readable over whatever screen is behind it. Engine-free so it
// is unit-tested off-engine; a drawer feeds it screen/font metrics and issues the screen->Dim() calls.
//
// Recovered and generalized from the (deleted) crash-consent panel helper — same tested math, reused
// for the auto-updater "open this link?" prompt. See features/updater and menu/messagebox.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_PROMPTPANEL_COMPUTE_H
#define ZX_PROMPTPANEL_COMPUTE_H

#include <cstdint>

namespace zx
{

struct PanelRect
{
	int x, y, w, h, radius;
};

// Centre a panel of width panelW horizontally on the screen and size it to wrap the content's
// vertical span [contentTop, contentBottom] with padY above and below, clamped to the screen.
// The corner radius is clamped so it never exceeds half of the panel's smaller side.
PanelRect ComputePanelRect(int screenW, int screenH, int panelW,
	int contentTop, int contentBottom, int padY, int radius);

// Top y that vertically centres a block of height blockH within screenH (clamped to >= 0 so an
// over-tall block simply starts at the top instead of going off-screen).
int ComputeCenteredTop(int screenH, int blockH);

// Horizontal inset (in px, >= 0) for a given row so the top/bottom corners follow a quarter
// circle of the panel's radius. Rows in the straight middle band return 0.
int ComputeRoundedInset(int row, int height, int radius);

struct PanelColor
{
	int r, g, b, a; // channels + alpha, each 0..255
};

// Sample a vertical linear gradient at row `row` of a `height`-tall panel (top -> bottom).
PanelColor ComputePanelGradient(int row, int height, PanelColor top, PanelColor bottom);

// Alpha for one column of a horizontal rule that fades out towards both ends: 0 at the extremes,
// rising to `peak` in the middle. Returns 0 for anything outside [0, width).
//
// Fading rather than a flat line because the rule sits INSIDE a panel with no border of its own -- a
// hard line would read as the panel being cut in two, where a fade reads as a divider between two
// parts of one surface. It is also what keeps the ends from colliding with the rounded corners
// without needing to know where those corners are.
int ComputeSeparatorAlpha(int x, int width, int peak);

} // namespace zx

#endif
