// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where every part of the picker's card goes.
//
// Extracted because a LAYOUT BUG shipped: the refusal line was anchored above the button while the
// rest of the panel flowed down from the top, and on a short card the two met -- "that server is not
// answering" drawn straight across the date. Nothing caught it because nothing could: the geometry
// was a hundred lines of arithmetic inside a Drawer, reachable only by opening the menu and looking.
//
// The rule the unit exists to hold is simple and was the one broken: THE FLOWING HALF HAS A FLOOR.
// Everything written downwards stops before the reason, the reason sits above the button, and the
// button is pinned to the bottom. Three regions that must not overlap, in a box whose height depends
// on how many rows there are and on how much the panel has to say.
//
// Header-pure by the features/ rules: no engine types, no font, no screen. Sizes arrive as numbers,
// so the whole card can be measured at any window size without one.
//
// What is NOT here: anything needing font metrics. Where a string is cut or wrapped depends on the
// width of the glyphs, which only the engine knows, so that stays at the drawing site.

#ifndef ZX_CONTINUECARD_COMPUTE_H
#define ZX_CONTINUECARD_COMPUTE_H

namespace zx
{

struct ContinueCardMetrics
{
	int virtualW;		// the space the card is laid out in
	int virtualH;
	int rowCount;		// rows the list has to show, before capping to what fits
	int rowHeight;
	int lineHeight;
	int maxVisibleRows;	// beyond this it scrolls rather than growing

	ContinueCardMetrics()
		: virtualW(640), virtualH(400), rowCount(0), rowHeight(23), lineHeight(10),
		  maxVisibleRows(7) {}
};

struct ContinueCardLayout
{
	int cardX, cardY, cardW, cardH;
	int listX, listY, listW;
	int panelX, panelY, panelW, panelH;
	int buttonX, buttonY, buttonW, buttonH;

	int visibleRows;	// how many actually fit

	// [rc4l] The floor for the panel's flowing half: the y below which nothing written from the top
	// may reach, because the refusal and then the button are there. Callers compare against this
	// before every section rather than trusting the panel to be tall enough.
	int panelFlowLimit;

	ContinueCardLayout()
		: cardX(0), cardY(0), cardW(0), cardH(0), listX(0), listY(0), listW(0),
		  panelX(0), panelY(0), panelW(0), panelH(0),
		  buttonX(0), buttonY(0), buttonW(0), buttonH(0), visibleRows(1), panelFlowLimit(0) {}
};

// `reasonLines` is how many lines of refusal the caller has wrapped, which is the only thing the
// floor depends on that the layout cannot work out for itself.
ContinueCardLayout ComputeContinueCard(const ContinueCardMetrics &m, int reasonLines);

} // namespace zx

#endif // ZX_CONTINUECARD_COMPUTE_H
