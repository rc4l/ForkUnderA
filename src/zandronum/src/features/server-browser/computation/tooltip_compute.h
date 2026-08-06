// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where a tooltip goes, and what it is made of.
//
// THE DATA STRUCTURE, because it is the whole design and not an implementation detail.
//
// A tooltip is not a property of a control. It is a property of a RECTANGLE that happened to be
// drawn. So the browser keeps a list of {rectangle, text} pairs which is CLEARED AT THE START OF
// EVERY FRAME and appended to by whatever draws each element, as it draws it. At the end of the
// frame the list is asked which region the pointer is inside, and that answer is the tooltip.
//
// Three things fall out of that, all of which the request asked for and none of which needed code:
//
//   - A GHOST TOOLTIP IS IMPOSSIBLE. A region only exists in the list because something drew it this
//     frame. Close the menu, reload the WADs, scroll the row away, switch tabs -- whatever stops
//     being drawn stops being registered, in the same frame, with no teardown to forget.
//   - THINGS YOU CANNOT CLICK CAN STILL EXPLAIN THEMSELVES. A WAD row is not a control and has no
//     hit test, but it is a rectangle, so it can have a tooltip. Same for a country flag or a ping.
//   - LATER WINS. Regions are tested in reverse, so a tooltip registered by something drawn on top
//     of another beats it -- which is the same order the eye resolves them in.
//
// The cost is one list append per hoverable thing per frame and a walk of it once. Both are trivial
// beside the drawing they accompany, and neither allocates after the first few frames.
//
// PLACEMENT is deliberately Windows': offset down and right of the pointer so the cursor does not
// sit on the text, flipped to the other side of the pointer when that would run off the screen, and
// clamped to a margin as a last resort. Flipping rather than merely clamping matters near a corner,
// where clamping alone would slide the box under the cursor.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_TOOLTIP_COMPUTE_H
#define ZX_TOOLTIP_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

struct TooltipBox
{
	int x, y, w, h;

	TooltipBox() : x(0), y(0), w(0), h(0) {}
};

// True when (px, py) is inside the half-open rectangle [x, x+w) x [y, y+h). Half-open so two
// rectangles that share an edge cannot both claim the same pixel.
bool TooltipRectContains( int x, int y, int w, int h, int px, int py );

// Split on '\n' into the lines to draw. An empty string gives no lines at all -- a tooltip with
// nothing in it should not be drawn, and returning one empty line would draw an empty box.
std::vector<std::string> TooltipLines( const std::string &text );

// Where to put a box of `contentW` x `contentH` for a pointer at (px, py) on a `screenW` x `screenH`
// screen. `offset` is how far down-right of the pointer it prefers to sit, `margin` how close to the
// screen edge it may come.
//
// A box too big for the screen is clamped to the margin and left oversized rather than shrunk: the
// caller decides what its content is, and silently cropping it here would hide the part that
// mattered.
TooltipBox ComputeTooltipPlacement( int px, int py, int contentW, int contentH,
	int screenW, int screenH, int offset, int margin );

} // namespace zx

#endif // ZX_TOOLTIP_COMPUTE_H
