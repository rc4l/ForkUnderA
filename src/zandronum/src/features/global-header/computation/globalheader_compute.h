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
};

const int kHeaderTabCount = 2;

// The bar's own geometry, in whatever virtual space the caller draws in.
//
// `barH` is the number everything else in the menu system depends on: it is how far every menu has
// to move down to stop the header landing on top of it. It is deliberately a little taller than the
// pills so the bar reads as a surface the tabs sit ON, rather than a row of buttons floating at the
// top of the screen.
struct HeaderMetrics
{
	int barH;      // full height of the bar, and the offset every menu below it owes
	int tabTop;    // y of the pills within the bar
	int tabH;      // pill height
	int leftPad;   // x where the first pill starts
	int gap;       // space between one pill and the next
	int labelPad;  // padding either side of a label inside its own pill

	HeaderMetrics() : barH(0), tabTop(0), tabH(0), leftPad(0), gap(0), labelPad(0) {}
};

// The shipped numbers, tuned against the 320-wide menu space the stock menus use.
HeaderMetrics DefaultHeaderMetrics();

struct HeaderRect
{
	int x, y, w, h;

	HeaderRect() : x(0), y(0), w(0), h(0) {}
	HeaderRect(int rx, int ry, int rw, int rh) : x(rx), y(ry), w(rw), h(rh) {}
};

// Where tab `index` sits, given the measured width of every label. An index outside the range
// answers an empty rect rather than reading past the array, because the caller that got the index
// wrong is exactly the caller that will not check.
HeaderRect HeaderTabRect(const HeaderMetrics &m, const int *labelWidths, int count, int index);

// Which tab the point is over, or -1 for none. Points anywhere on the bar but not on a pill are
// none: the bar's background is not a button.
int HeaderTabAtPoint(const HeaderMetrics &m, const int *labelWidths, int count, int px, int py);

// Is the point on the bar at all, pill or not? Used to keep a click on the bar's background from
// falling through to the menu underneath, which is still there and still listening.
bool HeaderBarContains(const HeaderMetrics &m, int py);

// Left and right along the bar.
//
// CLAMPS, does not wrap. With two tabs a wrap makes left and right do the same thing, and a bar
// whose ends you cannot feel is one you have to look at to use.
int StepHeaderTab(int index, int count, int step);

} // namespace zx

#endif // ZX_GLOBALHEADER_COMPUTE_H
