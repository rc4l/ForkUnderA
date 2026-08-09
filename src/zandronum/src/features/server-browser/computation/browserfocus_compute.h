// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where the arrow keys go in the server browser.
//
// The browser is a stack of regions, and the keyboard used to address only the list. Left and right
// switched tabs no matter what was highlighted, so there was no way to reach the JOIN button without
// a mouse, and no way to tell what a key was about to do.
//
// So the browser has a FOCUS: exactly one region owns the arrows at any moment, and the arrows either
// move within that region or hand it to a neighbour. The loop is deliberately small enough to hold in
// your head:
//
//     TABS                    PLAY | BROWSE
//       |
//     down
//       v
//     SUBTABS <--> SEARCH     Public | Private   [ search ]
//       |            |
//     down         down
//       v            v
//      ROWS  --right-->  ACTION
//       ^ |                |
//       | +-----left-------+
//       +---------up-------+   (up from ACTION returns to the sub-tabs)
//
//   TABS     left/right move along the top row; down enters the sub-tabs.
//   SUBTABS  left/right walk Public, Private, then the search box; up returns to the tabs; down
//            enters the list.
//   SEARCH   up returns to the sub-tabs; down enters the list. LEFT AND RIGHT ARE NOT NAVIGATION --
//            they belong to the caret, because a text field that jumped to another control when you
//            tried to move through what you had typed would be unusable.
//   ROWS     up/down move the selection; right goes to the button.
//   ACTION   left goes back to the list; up goes back to the sub-tabs.
//
// TWO ROWS, NOT ONE. This used to be a single row: PUBLIC, PRIVATE, HOST and the search box all
// walked with left and right. Hosting is not a filter on a list of servers, and having it sit beside
// two things that are made the row mean two jobs at once. Now the top row picks WHAT YOU ARE DOING
// (play or browse) and the second picks WHICH SERVERS, with the search box beside the sub-tabs
// because it filters them and belongs where the thing it filters is chosen.
//
// A TAB NEED NOT HAVE SUB-TABS. `subCount` of zero says this one does not, and then down from the
// tabs skips the row that is not there. That is how PLAY works: it has a form, not a list.
//
// Two rules that are easy to get wrong and are pinned by tests here:
//
//   - AN EMPTY LIST IS NOT ENTERABLE. Down with nothing listed must not move focus into a region with
//     no rows to land on, because everything downstream -- the detail panel, the JOIN button -- reads
//     a selection that would not exist.
//   - MOVEMENT AND TRAVERSAL ARE SEPARATE ANSWERS. A key that moves the selection does not change
//     focus, and a key that changes focus does not move the selection. Returning both from one call
//     is what stops the caller inventing its own rule for the overlap.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_BROWSERFOCUS_COMPUTE_H
#define ZX_BROWSERFOCUS_COMPUTE_H

namespace zx
{

// Which region owns the arrow keys.
enum class BrowserFocus
{
	Tabs,

	// [rc4l] The row under the tabs: which servers the list is showing. Only exists while the
	// selected tab has one, which is what `subCount` says.
	SubTabs,

	Search,
	Rows,
	Action,

	// [rc4l] The refresh button, in the footer at the bottom LEFT.
	//
	// It had no focus zone at all, so it was mouse-only: no arrow key reached it from anywhere, and
	// no amount of scrolling the list helped, because it is not in the list. A control that can only
	// be pressed with a pointer is not reachable for anyone driving the menu by keyboard or pad.
	//
	// Entered from the action button by going DOWN, which is a jump across the screen -- the button
	// is bottom left, the action button is on the right-hand panel -- but it is the only thing below
	// the action button, so down has nowhere else to mean.
	Refresh,

	// [rc4l] A modal dialog is up. It is a focus zone so the glow can anchor to its buttons with no
	// new machinery, but the ARROWS DO NOT TRAVERSE INTO OR OUT OF IT, that is what modal means, and
	// computation/dialog_compute owns what the keys do while it is on screen.
	Dialog,

	// [rc4l] The hosting form. Not modal, UP off the top of it returns to the tabs the same way
	// leaving any other region does, but it owns everything in between, because the form is a column
	// and down means "the next field", not "the next region".
	Host,
};

// The four keys this unit answers for. Enter and Escape are not navigation, they act on whatever the
// focus already is, which the caller knows better than this unit does.
enum class NavKey
{
	Up,
	Down,
	Left,
	Right,
};

// Where the two rows currently stand. A struct rather than a run of ints because there are now two
// positions and two counts, and four bare integers in a row is an argument order waiting to be got
// wrong silently.
struct NavWhere
{
	bool hasRows;   // the server list has something in it
	int tabIndex;   // position on the top row
	int tabCount;
	int subIndex;   // position on the sub-tab row
	int subCount;   // 0 when the selected tab has no sub-tab row at all

	NavWhere() : hasRows(false), tabIndex(0), tabCount(0), subIndex(0), subCount(0) {}
	NavWhere(bool rows, int tab, int tabs, int sub, int subs)
		: hasRows(rows), tabIndex(tab), tabCount(tabs), subIndex(sub), subCount(subs) {}
};

struct NavResult
{
	BrowserFocus focus;   // where focus ends up; unchanged when the key moved something instead
	int tabStep;          // -1 previous tab, +1 next tab, 0 no change
	int subStep;          // -1 previous sub-tab, +1 next sub-tab, 0 no change
	int rowStep;          // -1 previous row, +1 next row, 0 no change

	NavResult() : focus(BrowserFocus::Tabs), tabStep(0), subStep(0), rowStep(0) {}
};

// A focus of Rows with an empty list, or of SubTabs on a tab that has none, is treated as the
// nearest region that does exist. Either can happen underneath a focus that was legitimate when it
// was set, and the answer must still be one the caller can act on.
NavResult ComputeNav( BrowserFocus focus, NavKey key, const NavWhere &where );

} // namespace zx

#endif // ZX_BROWSERFOCUS_COMPUTE_H
