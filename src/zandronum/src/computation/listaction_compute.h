// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] A list with an action button beside it, and the keyboard's route between the two.
//
// Two menus in this tree are the same shape: the server browser's list and its JOIN button, and the
// Continue history's list and its CONTINUE button. Both need the same handful of edges -- go right
// to reach the button, come back left, and do not strand the focus on a button that is not there --
// and the second of them was about to get its own private copy of them.
//
// SHARED HERE, in the cross-feature computation folder, because a rule that exists twice is a rule
// that will be fixed once. The browser's own nav unit keeps everything peculiar to the browser (its
// tab rows, its search field, its refresh button); what it delegates is exactly the pair below.
//
// PRESSING THE LIST DOES NOT ACT. Enter on a row moves to the button rather than starting the thing,
// which is the difference between a list you can read and a list that launches under your hands. The
// button is where the decision is made, and it takes a deliberate second press -- exactly what the
// pointer has to do, so the two agree.
//
// KEYS THAT LEAVE THE PAIR ANSWER `Outside`, rather than this unit guessing. Up off the top of the
// list means the filter row in one menu and a wrap in the other; down off the button means the
// refresh button in one and the next row in the other. Those are layout, and layout belongs to the
// menu that has it.
//
// Header-pure by the features/ rules.

#ifndef ZX_LISTACTION_COMPUTE_H
#define ZX_LISTACTION_COMPUTE_H

namespace zx
{

enum class ListActionZone
{
	List,
	Action,

	// Not one of the pair. The caller decides what lies that way -- there may be nothing, and doing
	// nothing is a perfectly good answer.
	Outside,
};

enum class ListActionKey
{
	Up,
	Down,
	Left,
	Right,
	Enter,
};

struct ListActionStep
{
	ListActionZone zone;	// where focus ends up, or Outside
	int rowStep;			// -1 previous row, +1 next row, 0 no movement. Clamping is the caller's.
	bool activate;			// the button was pressed

	ListActionStep() : zone(ListActionZone::List), rowStep(0), activate(false) {}
};

// `actionAvailable` is whether the button exists AND can be pressed right now. With no button, Right
// and Enter keep the focus in the list rather than moving it somewhere undrawn -- a focus you cannot
// see is a menu the player has lost.
ListActionStep StepListAction(ListActionZone zone, ListActionKey key, bool actionAvailable);

} // namespace zx

#endif // ZX_LISTACTION_COMPUTE_H
