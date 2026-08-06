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

	TextInput() : caret(0) {}
	TextInput(const std::string &t, size_t c) : text(t), caret(c) {}
};

// True for the characters a field accepts. Space is printable; control characters and anything
// outside 7-bit ASCII are not -- the fonts this draws with have no glyphs for them, and a password
// silently containing a character the server cannot receive is worse than one that refuses it.
bool IsTypable( int ch );

// Insert at the caret, then step over what was inserted. Refuses to grow past `maxLength` and
// refuses anything IsTypable rejects -- in both cases the state comes back unchanged, so the caller
// does not need to pre-check.
TextInput InsertChar( const TextInput &in, int ch, size_t maxLength );

// Erase the character BEFORE the caret. At the start there is nothing before it, so nothing happens.
TextInput Backspace( const TextInput &in );

// Erase the character AT the caret. At the end there is nothing at it, so nothing happens.
TextInput DeleteForward( const TextInput &in );

// Move by `delta` characters, clamped to the ends. Left at the start and right at the end do
// nothing rather than wrapping: a caret that jumps to the far end of the line is never what was
// meant by pressing left once more.
TextInput MoveCaret( const TextInput &in, int delta );

// Caret to the start / to the end.
TextInput CaretHome( const TextInput &in );
TextInput CaretEnd( const TextInput &in );

// Empty text, caret at 0.
TextInput ClearInput( );

// Force any state into a valid one -- used when text arrives from outside the field (a remembered
// query, a pre-filled password) and its caret cannot be trusted.
TextInput NormaliseInput( const TextInput &in );

} // namespace zx

#endif // ZX_TEXTINPUT_COMPUTE_H
