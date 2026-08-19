// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where the arrow keys go inside a small uniform grid of buttons.
//
// The NEW tab's settings buttons were one row, so left and right were the only keys they had and a
// bound of "less than the count" was the whole rule. Two rows of two is a different question -- left
// and right must stay on their row, up and down must cross between them, and only the edges hand the
// keyboard back to the screen -- and writing that out at four call sites is four chances to get one
// edge wrong in a way nobody notices until the cursor walks off the grid.
//
//     LEFT         moves along the row and stops at its start; it never wraps onto another row,
//                  because sideways off the end of a row is not a direction anybody means.
//     RIGHT        moves along the row, and off the right-hand end LEAVES -- there IS something
//                  over there, the foot's buttons, and stopping dead at the edge made the grid a
//                  place the keyboard could enter and only leave upwards.
//     UP           crosses to the row above, and off the top LEAVES -- the caller hands the arrows
//                  to whatever sits above the grid.
//     DOWN         crosses to the row below and stops at the bottom, the same rule the foot's
//                  buttons follow: nothing is below, so nothing moves.
//
// `leaves` says the grid is finished with the key, not where the key goes: the caller passed the
// direction in and is the only side that knows what lies that way.
//
// A short last row is handled rather than assumed: three buttons in a 2-wide grid leaves one alone
// on the bottom, and RIGHT from it must not select a cell that is not drawn.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_TOOLGRID_COMPUTE_H
#define ZX_TOOLGRID_COMPUTE_H

namespace zx
{

enum class GridKey
{
	Left,
	Right,
	Up,
	Down,
};

struct GridMove
{
	int sel;		// where the cursor ends up; unchanged when the key did nothing
	bool leaves;	// the key belongs to whatever is above the grid, not to the grid

	GridMove() : sel(0), leaves(false) {}
	GridMove(int s, bool l) : sel(s), leaves(l) {}
};

// [rc4l] `cols` is how many cells are on a full row; `count` is how many there actually are, which
// may leave the last row short.
GridMove ComputeGridMove(int sel, int count, int cols, GridKey key);

// [rc4l] Where RIGHT lands when it leaves a list that has the load order beside it.
//
// The load order is what is over there, so that is where the key goes -- but an empty one has no row
// to land on, and focus on a row that is not drawn is the invisible-but-reachable bug in its
// keyboard form. So an empty order is skipped entirely and the key carries on to the foot, which is
// where the grid's right-hand edge sends it too: one destination for "rightwards, out of here".
enum class RightExit
{
	LoadOrder,	// its topmost row
	Foot,		// the buttons under it, leftmost first
};

RightExit ComputeRightExitFromList(bool bLoadOrderHasRows);

} // namespace zx

#endif // ZX_TOOLGRID_COMPUTE_H
