// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] A row of mutually exclusive choices -- pick one, and picking one unpicks the others.
//
// WHY THIS IS NOT A TOGGLE. The browser already had a two-state control, drawn as one line of text
// that swapped when clicked. It works and it is a lie about the shape of the question: a toggle says
// "this thing is on or off", and "local or global" is not one thing being switched, it is two
// answers of which exactly one is true. A player looking at a toggle has to click it to find out
// what the other state even IS. A row shows both and marks which is chosen.
//
// It is also the general case. Two options today; a game mode picker is three or four, and the only
// thing that changes is the length of the array.
//
// WHAT LIVES HERE is everything that is arithmetic: where each option sits, which one a pointer is
// over, and what an arrow key does to the selection. What does not is the drawing -- the caller owns
// that, because it owns the panel the row is in.
//
// THE INVARIANT: exactly one option is selected, always. There is no "none", because a question with
// no answer is not a state this control can be left in -- every caller has a default, and an index
// that arrives out of range is brought back rather than honoured.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_CHOICEROW_COMPUTE_H
#define ZX_CHOICEROW_COMPUTE_H

namespace zx
{

// Where one option's box sits, in the caller's units.
struct ChoiceCell
{
	int x;
	int width;
	bool valid;

	ChoiceCell() : x(0), width(0), valid(false) {}
};

// [rc4l] Equal cells across `totalWidth`, separated by `gap`.
//
// Equal rather than sized to their labels: a row where "Anyone on the internet" is three times the
// width of "This network only" reads as one being the important one, and the whole point is that
// they are alternatives of equal standing. The remainder from an uneven division goes to the last
// cell, so the row always ends exactly where it was told to.
ChoiceCell ChoiceCellAt(int index, int count, int x, int totalWidth, int gap);

// Which option contains `px`, or -1 for the gaps and everything outside. The gaps deliberately
// belong to nobody: a click that lands between two answers is not evidence for either.
int ChoiceHitTest(int px, int count, int x, int totalWidth, int gap);

// Where an arrow key moves the selection. Does NOT wrap.
//
// A ring is right for tabs, where the row is the whole world and going round is how you get back. It
// is wrong here: this row sits inside a form, the arrows that reach it also leave it, and a selection
// that silently wrapped from one end to the other would change the answer while the player was
// trying to move past it.
int ChoiceStep(int selected, int count, int step);

// Bring an index into range. Anything outside becomes 0 -- the caller's default -- because "no
// selection" is not a state this control has.
int ChoiceNormalise(int selected, int count);

} // namespace zx

#endif // ZX_CHOICEROW_COMPUTE_H
