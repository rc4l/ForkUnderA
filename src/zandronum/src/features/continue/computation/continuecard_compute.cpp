// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuecard_compute.h"

namespace zx
{

namespace
{

// The card in the 640-wide space it is designed for. Narrower windows get a narrower card rather
// than a clipped one.
const int kCardW = 570;
const int kListW = 320;
const int kColGap = 12;
const int kPadX = 14;
const int kDotW = 9;			// the status dot's lane, left of the text
const int kHeaderH = 46;		// title, column headings and the rule under them
const int kFooterH = 10;
const int kButtonH = 15;

// [rc4l] The panel's minimum height. Sized only from the rows, a four-row history gave the panel
// less room than a name, a mode, an address, a date and a refusal need -- which is exactly how the
// refusal came to be drawn over the date.
const int kPanelFloorH = 112;

int Clamp(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

} // namespace

ContinueCardLayout ComputeContinueCard(const ContinueCardMetrics &m, int reasonLines)
{
	ContinueCardLayout out;

	const int rowH = (m.rowHeight > 0) ? m.rowHeight : 1;
	const int lineH = (m.lineHeight > 0) ? m.lineHeight : 1;
	const int maxRows = (m.maxVisibleRows > 0) ? m.maxVisibleRows : 1;

	// Shorter when there is less to show: a card sized for seven rows with three in it is a box of
	// empty space with a list at the top of it.
	out.visibleRows = Clamp(m.rowCount, 1, maxRows);

	out.cardW = (kCardW < m.virtualW - 40) ? kCardW : (m.virtualW - 40);
	if (out.cardW < 160)
		out.cardW = 160;		// a window too narrow for the card still gets a card

	// As tall as the taller of its two columns, because the panel has a floor the list may not reach.
	const int listH = out.visibleRows * rowH;
	out.cardH = kHeaderH + ((listH > kPanelFloorH) ? listH : kPanelFloorH) + kFooterH;

	out.cardX = (m.virtualW - out.cardW) / 2;
	out.cardY = (m.virtualH - out.cardH) / 2;

	out.listX = out.cardX + kPadX + kDotW;
	out.listY = out.cardY + kHeaderH;
	out.listW = kListW - kDotW;

	out.panelX = out.cardX + kPadX + kListW + kColGap;
	out.panelY = out.listY - 4;
	out.panelW = out.cardW - kPadX - (out.panelX - out.cardX);
	out.panelH = (listH + 4 > kPanelFloorH) ? (listH + 4) : kPanelFloorH;

	out.buttonH = kButtonH;
	out.buttonW = out.panelW - 12;
	out.buttonX = out.panelX + 6;
	out.buttonY = out.panelY + out.panelH - out.buttonH - 6;

	// [rc4l] The floor. Three regions stack from the bottom -- the button, the refusal above it, and
	// whatever flows down from the top -- and this is the line the third must not cross.
	const int lines = (reasonLines > 0) ? reasonLines : 0;
	out.panelFlowLimit = out.buttonY - 3 - (lines * lineH);

	return out;
}

} // namespace zx
