// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The editing rules for a one-line text field.
//
// The engine has no reusable text box. DOptionMenuFieldBase edits a CVAR through the option-menu
// machinery, which is the wrong shape for a search box that filters a list as you type and for a
// password prompt that has to hand its value to a join -- neither is a setting, and neither lives in
// an option menu. So the rules live here, engine-free and testable, and the drawing and key plumbing
// stay in the menu.
//
// Every operation RETURNS A NEW STATE rather than mutating one. A field is two values that must agree
// -- the text and where the caret is in it -- and the bugs in hand-written editors are almost always
// one of them being updated without the other. Returning both together makes that impossible to get
// half-right, and makes every case assertable in a test.
//
// The caret is a position BETWEEN characters, so it ranges over [0, length] inclusive: 0 is before
// the first character and length is after the last. Every operation keeps it inside that range no
// matter what it is handed.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_TEXTINPUT_COMPUTE_H
#define ZX_TEXTINPUT_COMPUTE_H

#include <cstddef>
#include <string>

namespace zx
{

struct TextInput
{
	std::string text;
	size_t caret;

	// The other end of the selection. Equal to the caret means no selection -- which is why there is
	// no separate "has a selection" flag to keep in step with the two positions that decide it.
	size_t anchor;

	TextInput() : caret(0), anchor(0) {}
	TextInput(const std::string &t, size_t c) : text(t), caret(c), anchor(c) {}
	TextInput(const std::string &t, size_t c, size_t a) : text(t), caret(c), anchor(a) {}
};

// The selected range, always ordered -- the caret can be either side of the anchor depending on
// which way it was dragged, and every caller wants it the same way round.
size_t SelectionStart( const TextInput &in );
size_t SelectionEnd( const TextInput &in );
bool HasSelection( const TextInput &in );
std::string SelectedText( const TextInput &in );

// Erase the selection, leaving the caret where it began. Nothing selected, nothing happens.
TextInput DeleteSelection( const TextInput &in );

// Everything, for Ctrl+A. The caret goes to the end, as it does in every text field.
TextInput SelectAll( const TextInput &in );

// Put the caret at an absolute position -- what a mouse click and a drag do. `extend` keeps the
// anchor where it was, which is what turns a drag into a selection and shift+click into one too.
TextInput SetCaret( const TextInput &in, size_t pos, bool extend );

// Insert a whole string at the caret, replacing any selection. For paste. Characters IsTypable
// rejects are DROPPED rather than refusing the paste: clipboards routinely carry a trailing newline,
// and refusing the whole paste over it would be a puzzle rather than a safeguard.
TextInput InsertText( const TextInput &in, const std::string &text, size_t maxLength );

// True for the characters a field accepts. Space is printable; control characters and anything
// outside 7-bit ASCII are not -- the fonts this draws with have no glyphs for them, and a password
// silently containing a character the server cannot receive is worse than one that refuses it.
bool IsTypable( int ch );

// Insert at the caret, REPLACING any selection, then step over what was inserted. Refuses to grow
// past `maxLength` and refuses anything IsTypable rejects -- in both cases the state comes back
// unchanged, so the caller does not need to pre-check.
TextInput InsertChar( const TextInput &in, int ch, size_t maxLength );

// Erase the selection if there is one, otherwise the character BEFORE the caret. At the start of an
// unselected field there is nothing before it, so nothing happens.
TextInput Backspace( const TextInput &in );

// Erase the selection if there is one, otherwise the character AT the caret.
TextInput DeleteForward( const TextInput &in );

// Move by `delta` characters, clamped to the ends. Left at the start and right at the end do
// nothing rather than wrapping: a caret that jumps to the far end of the line is never what was
// meant by pressing left once more.
//
// `extend` is shift being held. Without it the selection collapses -- and an unshifted arrow key on
// a selection goes to the END it was pressed towards, not one character past wherever the caret
// happened to be, which is what every text field does and what makes arrowing off a selection feel
// right.
TextInput MoveCaret( const TextInput &in, int delta, bool extend );

// [rc4l] Whether an arrow should LEAVE the field instead of moving the caret inside it.
//
// A text field has to claim left and right, or you cannot move through what you typed. It also has
// to give them back at some point, or a box with something beside it becomes a place the keyboard
// goes and never comes out of. The answer everywhere else in software is the same: the arrow moves
// the caret until the caret has nowhere left to go, and the next press moves on.
//
// `hasNeighbour` is the caller's -- only it knows what is beside this box, and a field with nothing
// there must keep the key rather than hand it to nobody.
//
// Shift NEVER leaves: shift+arrow is a selection gesture, and one that jumped to another control
// halfway through would be unusable. Nor does an arrow with a selection live -- that press collapses
// the selection, which is a move in its own right, and only the press after it reaches the edge.
bool ArrowLeavesField( const TextInput &in, bool goingRight, bool hasNeighbour, bool shiftHeld );

// Caret to the start / to the end, extending the selection if shift is held.
TextInput CaretHome( const TextInput &in, bool extend );
TextInput CaretEnd( const TextInput &in, bool extend );

// Ctrl+arrow: to the start of the previous word, or past the end of the next one.
//
// "Word" is the usual rule and worth stating because the edges are where these always feel wrong:
// moving left skips any run of spaces first and then the run of non-spaces before it, so a caret
// after "brutal doom" lands before "doom" and then before "brutal", never in the gap between them.
// Moving right is the mirror -- over the current word, then over the spaces after it.
TextInput MoveWord( const TextInput &in, bool forward, bool extend );

// The word under `pos`, for double-click. An empty range when `pos` is on a space, because there is
// no word there to select and picking a neighbouring one would be a guess.
TextInput SelectWordAt( const TextInput &in, size_t pos );

// What a double-click in the FIELD means, which is not quite the same question.
//
// On a word, that word. Anywhere else -- the blank part of the box past the end of the text, or a
// gap between words -- EVERYTHING. Selecting nothing there would be technically defensible and
// useless: a double-click is a request to grab something, and the only sensible thing to grab when
// the pointer is not on a word is the lot.
TextInput SelectWordOrAll( const TextInput &in, size_t pos );

// Empty text, caret at 0.
TextInput ClearInput( );

// Force any state into a valid one -- used when text arrives from outside the field (a remembered
// query, a pre-filled password) and its caret cannot be trusted.
TextInput NormaliseInput( const TextInput &in );

} // namespace zx

#endif // ZX_TEXTINPUT_COMPUTE_H
