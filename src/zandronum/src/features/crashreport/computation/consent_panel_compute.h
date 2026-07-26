// [rc4l] Pure geometry/color math for the crash-consent menu's background panel (rounded corners
// + vertical gradient). Kept engine-free so it is unit-tested off-engine; the drawer in
// zx_consentmenu.cpp feeds it screen/font metrics and issues the actual screen->Dim() calls.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#pragma once

#include <cstdint>

namespace zx
{

struct ConsentPanelRect
{
	int x, y, w, h, radius;
};

// Centre a panel of width panelW horizontally on the screen and size it to wrap the content's
// vertical span [contentTop, contentBottom] with padY above and below, clamped to the screen.
// The corner radius is clamped so it never exceeds half of the panel's smaller side.
ConsentPanelRect ComputeConsentPanelRect(int screenW, int screenH, int panelW,
	int contentTop, int contentBottom, int padY, int radius);

// Horizontal inset (in px, >= 0) for a given row so the top/bottom corners follow a quarter
// circle of the panel's radius. Rows in the straight middle band return 0.
int ComputeRoundedInset(int row, int height, int radius);

struct ConsentPanelColor
{
	int r, g, b, a; // channels + alpha, each 0..255
};

// Sample a vertical linear gradient at row `row` of a `height`-tall panel (top -> bottom).
ConsentPanelColor ComputeConsentGradient(int row, int height,
	ConsentPanelColor top, ConsentPanelColor bottom);

} // namespace zx
