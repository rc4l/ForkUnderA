// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where the tabs sit on the bar that runs across the top of every menu.
//
// The bar is GLOBAL: it is drawn over whatever menu is open, so its geometry cannot be expressed in
// any one menu's terms. It gets its own arithmetic here, in a virtual space the caller supplies,
// and both the drawing and the mouse ask this same unit where things are. That is the point of the
// unit: a hit-test that disagrees with the drawing by three pixels is invisible in a screenshot and
// obvious the first time somebody clicks.
//
// PILLS SIZED TO THEIR LABELS, not to a fixed grid. There are only ever a handful of tabs and their
// names are very different lengths ("Main Menu", "Play Online!"), so an even split leaves one pill
// padded with air and crowds the other. The caller measures its own font and passes the widths in,
// which is also what keeps this header-pure.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_GLOBALHEADER_COMPUTE_H
#define ZX_GLOBALHEADER_COMPUTE_H

namespace zx
{

// The tabs, in the order they appear. Main Menu first because it is where Escape lands and the eye
// reads left to right from the thing it already has.
enum class HeaderTab
{
	MainMenu,
	PlayOnline,
	Continue,
};

// [rc4l] Continue is LAST in this enum and FIRST on the screen, and the two orders are deliberately
// allowed to disagree.
//
// Everything else about the bar is keyed on these values -- which tab is lit, which one the mouse
// found, what the arrows step through -- so inserting Continue at the front would have renumbered
// the two tabs that were already there and moved them for the sake of a third that is usually
// absent. Appending leaves them exactly where they were, centred, and lets the LAYOUT say where the
// new one is drawn. `pinnedIndex` below is how that is said.
const int kHeaderTabCount = 3;

// The count when there is nothing to continue, which is most of the time. The centred pair are
// indices 0 and 1, so a shorter count simply stops before the pinned one exists.
const int kHeaderTabCountNoContinue = 2;

// The bar's own geometry, in whatever virtual space the caller draws in.
//
// `barH` is the number everything else in the menu system depends on: it is how far every menu has
// to move down to stop the header landing on top of it. It is deliberately a little taller than the
// pills so the bar reads as a surface the tabs sit ON, rather than a row of buttons floating at the
// top of the screen.
struct HeaderMetrics
{
	int barH;      // full height of the bar
	int tabTop;    // y of the pills within the bar
	int tabH;      // pill height
	int leftPad;   // x where the first pill starts
	int gap;       // space between one pill and the next
	int labelPad;  // padding either side of a label inside its own pill
	int menuGap;   // clear air between the bar's bottom edge and whatever the menu draws first

	// The focus orb, which is drawn OUTSIDE the pill it marks: `glowInset` left of the pill's edge,
	// and `glowRadius` across from there. Both live here so the padding either side of a pill can be
	// checked against the thing that has to fit in it, rather than being a number that looked about
	// right until the orb went half off the screen.
	int glowInset;
	int glowRadius;

	// How wide the bar is, in the caller's own space, so the row of tabs can be centred on it. Left
	// at zero the row starts at leftPad instead, which is what a caller that has not said gets.
	int barW;

	// [rc4l] THE DIAL. How big the whole bar is drawn, as a percentage: 100 is one bar unit per half
	// a stock menu unit, 125 is a quarter larger again.
	//
	// It is a zoom on the SPACE rather than a multiplier on the sizes, which is the only version that
	// works. Growing the numbers here would grow the pills and leave the labels, the orb and the
	// corner radii at their old size inside them; zooming the space carries all of it together, and
	// the one thing that must not move with it -- how far the menus below are pushed down -- is
	// derived from this same number in MenuClearanceY.
	int zoomPercent;

	HeaderMetrics()
		: barH(0), tabTop(0), tabH(0), leftPad(0), gap(0), labelPad(0), menuGap(0),
		  glowInset(0), glowRadius(0), barW(0), zoomPercent(100) {}
};

// The shipped numbers, in the 640x400 virtual space the server browser already draws in. That space
// rather than the stock menus' 320x200 because the bar has to sit against the browser's chrome and
// match it pill for pill; the stock menus are the ones that get shifted, and a shift is one number.
HeaderMetrics DefaultHeaderMetrics();

// How far every menu below the bar has to move down, in the stock menus' own 320x200 units.
//
// DERIVED, never written down twice. The bar's height is going to change, and a clearance kept as
// its own number is one that quietly stops matching the thing it exists to clear: the first symptom
// is a menu title resting on the bar, which is what happened when this was the bar height exactly
// and left no air at all.
//
// The halving is the change of space, 640x400 down to 320x200, and it rounds UP because half a pixel
// of overlap is still overlap.
int MenuClearanceY(const HeaderMetrics &m);

struct HeaderRect
{
	int x, y, w, h;

	HeaderRect() : x(0), y(0), w(0), h(0) {}
	HeaderRect(int rx, int ry, int rw, int rh) : x(rx), y(ry), w(rw), h(rh) {}
};

// [rc4l] ONE TAB MAY BE PINNED TO THE LEFT EDGE while the rest stay centred.
//
// Continue is not a peer of the other tabs. It is a way back into a session, it comes and goes with
// whether there is one, and the two that are always there must not shuffle sideways every time it
// appears. Pinning it to the left edge gives it a fixed home of its own and leaves the centred pair
// exactly where the player last saw them.
//
// `pinnedIndex` is -1 for none. Everything below takes it, because the drawing, the hit-test and the
// arrow keys all have to agree about where the pill went, and a layout only half of them know about
// is a click that lands on nothing.
int HeaderRowWidth(const HeaderMetrics &m, const int *labelWidths, int count, int pinnedIndex);
int HeaderRowLeft(const HeaderMetrics &m, const int *labelWidths, int count, int pinnedIndex);

// Where tab `index` sits, given the measured width of every label. An index outside the range
// answers an empty rect rather than reading past the array, because the caller that got the index
// wrong is exactly the caller that will not check.
HeaderRect HeaderTabRect(const HeaderMetrics &m, const int *labelWidths, int count, int index,
	int pinnedIndex);

// Which tab the point is over, or -1 for none. Points anywhere on the bar but not on a pill are
// none: the bar's background is not a button.
int HeaderTabAtPoint(const HeaderMetrics &m, const int *labelWidths, int count, int px, int py,
	int pinnedIndex);

// Left and right in the order the EYE sees, which is the pinned tab first and then the centred row.
// Without this the arrows would step in enum order and jump over the middle of the bar.
int StepHeaderTabPinned(int index, int count, int pinnedIndex, int step);

// Is the point on the bar at all, pill or not? Used to keep a click on the bar's background from
// falling through to the menu underneath, which is still there and still listening.
bool HeaderBarContains(const HeaderMetrics &m, int py);

// Left and right along the bar.
//
// CLAMPS, does not wrap. With two tabs a wrap makes left and right do the same thing, and a bar
// whose ends you cannot feel is one you have to look at to use.
int StepHeaderTab(int index, int count, int step);

// Is the menu's cursor on the first row the player can actually reach, so that Up should leave the
// menu and land on the bar instead of wrapping round to the bottom?
//
// FIRST SELECTABLE, NOT INDEX ZERO. Menus lead with things the cursor never visits: a title patch, a
// line of static text, a greyed-out entry. The top row is wherever the first reachable item happens
// to be, and hard-coding zero would mean Up wraps to the bottom on exactly the menus that have a
// banner, which is most of them.
//
// Answers false when nothing is selected: the menu's own Up already means "pick a row" then, and
// stealing it would leave a menu the keyboard can never enter.
bool CursorAtTopRow(const bool *selectable, int count, int selected);

} // namespace zx

#endif // ZX_GLOBALHEADER_COMPUTE_H
