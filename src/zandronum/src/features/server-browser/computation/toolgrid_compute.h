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

// [rc4l] WHERE UP AND DOWN GO INSIDE A MODAL, which is a body and a row of buttons under it.
//
// Every box on the hosting screens is that shape -- the map rotation, FLAGS, GAMEPLAY, the SERVER
// box -- and every one of them got it wrong in its own way: some clamped at the top so the footer
// could not be reached at all, some clamped at the bottom, and the one that did hand over did it in
// only one direction. A box you can enter and not leave by the same key you entered with is the
// same bug wearing four coats.
//
// So it is one rule: the body and the footer are a LOOP. Off either end of the body is the footer,
// off either end of the footer is the body, and which end you arrive at is the one nearest where
// you came from. A body with nothing in it is not somewhere focus may sit, so the footer keeps the
// key rather than handing it to an empty list.
enum class ModalRegion
{
	Body,
	Footer,
};

struct ModalPos
{
	ModalRegion region;
	int index;

	ModalPos() : region(ModalRegion::Body), index(0) {}
	ModalPos(ModalRegion r, int i) : region(r), index(i) {}
};

// `step` is -1 for up and +1 for down.
ModalPos ComputeModalStep(ModalPos pos, int bodyCount, int footCount, int step);

// [rc4l] Walking a vertical list that has somewhere to go off either end.
//
// A list that only ever clamps is a region the keyboard can enter and not leave, which is the same
// bug as focus landing on something undrawn wearing different clothes -- and an EMPTY list is not
// enterable at all, so it hands the key straight on rather than swallowing it.
enum class ListStep
{
	Move,		// stay in the list, one row along
	LeaveUp,
	LeaveDown,
};

ListStep ComputeListStep(int sel, int count, int step);

// [rc4l] Where the foot's row of buttons hands the keyboard on.
//
// LEFT off the leftmost button is the settings grid, which sits on the same line in the other
// column -- the foot is the bottom right of the screen and that is genuinely what is beside it.
//
// UP is the load order above it. An EMPTY order has no row to hold focus, and the answer then is the
// IWAD row rather than the settings grid: with nothing loaded there is nothing to play, so the way
// out of the foot is the top of the screen where a setup starts, not the box of settings for a
// server that cannot be started yet.
enum class FootExit
{
	StayOnRow,		// the caller moves along the row itself
	SettingsGrid,
	LoadOrder,
	IwadRow,
};

FootExit ComputeFootExit(GridKey key, int sel, bool bLoadOrderHasRows);

} // namespace zx

#endif // ZX_TOOLGRID_COMPUTE_H
