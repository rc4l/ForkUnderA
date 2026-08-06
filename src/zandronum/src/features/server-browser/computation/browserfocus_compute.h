// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where the arrow keys go in the server browser.
//
// The browser is three things stacked and side by side -- a row of tabs, a list of servers, and a
// panel with one button on it -- and the keyboard used to address only the list. Left and right
// switched tabs no matter what was highlighted, so there was no way to reach the JOIN button without
// a mouse, and no way to tell what a key was about to do.
//
// So the browser has a FOCUS: exactly one of those three regions owns the arrows at any moment, and
// the arrows either move within that region or hand it to a neighbour. The loop is deliberately
// small enough to hold in your head:
//
//     TABS <--> SEARCH
//       |          |
//     down       down
//       v          v
//      ROWS  --right-->  ACTION
//       ^ |                |
//       | +-----left-------+
//       +---------up-------+   (up from ACTION returns to the tabs)
//
//   TABS    left/right move along the top row; down enters the list.
//   SEARCH  up returns to the tabs; down enters the list. LEFT AND RIGHT ARE NOT NAVIGATION --
//           they belong to the caret, because a text field that jumped to another control when you
//           tried to move through what you had typed would be unusable.
//   ROWS    up/down move the selection; right goes to the button.
//   ACTION  left goes back to the list; up goes back to the tabs.
//
// THE TOP ROW IS A ROW. The tabs and the search box sit side by side on it, so left and right walk
// along it -- PUBLIC, PRIVATE, then the search box -- rather than the tabs wrapping among
// themselves. Wrapping was fine when the tabs were the only thing up there; with a third stop on the
// same line, a key that skipped past it to loop back would leave the box unreachable.
//
// Two rules that are easy to get wrong and are pinned by tests here:
//
//   - AN EMPTY LIST IS NOT ENTERABLE. Down from the tabs with nothing listed must not move focus into
//     a region with no rows to land on, because everything downstream -- the detail panel, the JOIN
//     button -- reads a selection that would not exist. Focus stays on the tabs.
//   - MOVEMENT AND TRAVERSAL ARE SEPARATE ANSWERS. A key that moves the selection does not change
//     focus, and a key that changes focus does not move the selection. Returning both from one call
//     is what stops the caller inventing its own rule for the overlap.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_BROWSERFOCUS_COMPUTE_H
#define ZX_BROWSERFOCUS_COMPUTE_H

namespace zx
{

// Which region owns the arrow keys.
enum class BrowserFocus
{
	Tabs,
	Search,
	Rows,
	Action,
};

// The four keys this unit answers for. Enter and Escape are not navigation -- they act on whatever
// the focus already is, which the caller knows better than this unit does.
enum class NavKey
{
	Up,
	Down,
	Left,
	Right,
};

struct NavResult
{
	BrowserFocus focus;   // where focus ends up; unchanged when the key moved something instead
	int tabStep;          // -1 previous tab, +1 next tab, 0 no change
	int rowStep;          // -1 previous row, +1 next row, 0 no change
};

// `hasRows` is whether the list currently has anything in it. A focus of Rows with an empty list is
// treated as Tabs -- the list can empty out underneath a focus that was legitimate when it was set,
// and the answer must still be one the caller can act on.
//
// `onLastTab` says whether the tab currently selected is the last one on the row, which is what
// decides whether Right switches tab or steps off the end into the search box. The caller knows
// which tab it is on; this unit does not need to, and asking for just the one bit keeps it from
// having to learn how many tabs there are.
NavResult ComputeNav( BrowserFocus focus, NavKey key, bool hasRows, bool onLastTab );

} // namespace zx

#endif // ZX_BROWSERFOCUS_COMPUTE_H
