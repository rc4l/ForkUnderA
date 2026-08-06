// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The decisions a modal dialog makes, apart from how it looks.
//
// The browser had exactly one modal -- "cancel this download?" -- and it was the one part of the
// screen that did not look like the rest: bare text on a dim, no panel, no buttons, and a pair of
// letters to press. Every other control in the browser is a rounded button you can click, arrow to,
// or read a tooltip off. A question is not a good place to make the player learn a second set of
// rules.
//
// So this is shared, and deliberately owns the RULES rather than the pixels:
//
//   - Which choice has focus, and what an arrow key does to that.
//   - Which choice a typed letter picks -- because a shortcut has to work at the same time as the
//     arrows and the mouse, not instead of them.
//   - What Escape means, which is never "whichever button happens to be focused". Backing out of a
//     question has one answer and it is the safe one.
//
// The caller supplies the choices and draws them. It gets back an index, and an index is the only
// thing the two shapes of dialog -- a row of buttons, and a text field with OK/Cancel under it --
// need to have in common.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_DIALOG_COMPUTE_H
#define ZX_DIALOG_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx
{

enum class DialogKey
{
	Left,
	Right,
	Up,
	Down,
};

// Move the focus among `count` choices. Wraps, because a row of two or three buttons is a ring in
// the player's head and stopping dead at the end of it just feels broken.
//
// Up and down do the same as left and right: the buttons are in a row, but a player who has been
// arrowing DOWN a server list a moment ago should not have to notice that the dialog changed axis.
// Nonsense input -- a negative focus, an empty dialog -- comes back as something safe to index with.
int ComputeDialogFocus( int focus, int count, DialogKey key );

// Which choice a typed character picks, or -1 for none. Case-insensitive: a player holding shift is
// still answering the question.
int ComputeDialogShortcut( const std::vector<char> &shortcuts, int ch );

// Which choice Escape resolves to.
//
// ALWAYS THE CANCELLING ONE, never whatever has focus. Escape means "I did not want this", and a
// dialog where backing out could confirm a destructive thing depending on where the highlight had
// drifted is a trap. `cancelIndex` is the caller's declared safe answer; out of range means Escape
// does nothing at all, which is better than guessing.
int ComputeDialogEscape( int cancelIndex, int count );

} // namespace zx

#endif // ZX_DIALOG_COMPUTE_H
