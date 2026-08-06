// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/textinput_compute.h"

namespace zx
{

namespace
{
size_t Clamp( size_t at, size_t length )
{
	return ( at > length ) ? length : at;
}

// Add a signed delta to a position without letting it wrap at either end. Checked as a subtraction
// on the way up because caret + delta overflows on a large delta, and a position before the start of
// the string is a crash waiting for the next keystroke.
size_t Step( size_t at, int delta, size_t length )
{
	if ( delta < 0 )
	{
		const size_t back = static_cast<size_t>( -static_cast<long long>( delta ));
		return ( back >= at ) ? 0 : ( at - back );
	}

	const size_t forward = static_cast<size_t>( delta );
	return ( forward > ( length - at )) ? length : ( at + forward );
}
} // namespace

bool IsTypable( int ch )
{
	// Printable 7-bit ASCII, space included. Deliberately not isprint(): that is locale-dependent,
	// and a field whose accepted characters change with the machine's locale is not a field anyone
	// can reason about.
	return ( ch >= 32 ) && ( ch < 127 );
}

size_t SelectionStart( const TextInput &in )
{
	const TextInput at = NormaliseInput( in );
	return ( at.caret < at.anchor ) ? at.caret : at.anchor;
}

size_t SelectionEnd( const TextInput &in )
{
	const TextInput at = NormaliseInput( in );
	return ( at.caret > at.anchor ) ? at.caret : at.anchor;
}

bool HasSelection( const TextInput &in )
{
	return SelectionStart( in ) != SelectionEnd( in );
}

std::string SelectedText( const TextInput &in )
{
	const size_t from = SelectionStart( in );
	const size_t to = SelectionEnd( in );
	return NormaliseInput( in ).text.substr( from, to - from );
}

TextInput DeleteSelection( const TextInput &in )
{
	TextInput out = NormaliseInput( in );

	const size_t from = SelectionStart( out );
	const size_t to = SelectionEnd( out );
	if ( from == to )
		return out;

	out.text.erase( from, to - from );
	out.caret = from;
	out.anchor = from;
	return out;
}

TextInput SelectAll( const TextInput &in )
{
	TextInput out = NormaliseInput( in );
	out.anchor = 0;
	out.caret = out.text.size( );
	return out;
}

TextInput SetCaret( const TextInput &in, size_t pos, bool extend )
{
	TextInput out = NormaliseInput( in );

	out.caret = Clamp( pos, out.text.size( ));
	if ( !extend )
		out.anchor = out.caret;

	return out;
}

TextInput InsertChar( const TextInput &in, int ch, size_t maxLength )
{
	if ( !IsTypable( ch ))
		return NormaliseInput( in );

	// Typing over a selection replaces it, which is the one behaviour every text field shares and the
	// one people notice instantly when it is missing.
	TextInput out = DeleteSelection( in );

	if ( out.text.size( ) >= maxLength )
		return out;

	out.text.insert( out.caret, 1, static_cast<char>( ch ));
	++out.caret;
	out.anchor = out.caret;
	return out;
}

TextInput InsertText( const TextInput &in, const std::string &text, size_t maxLength )
{
	TextInput out = DeleteSelection( in );

	for ( size_t i = 0; i < text.size( ); ++i )
	{
		// Dropped, not refused. A clipboard routinely carries a trailing newline, and rejecting the
		// whole paste over one would be a puzzle rather than a safeguard.
		if ( !IsTypable( text[i] ))
			continue;
		if ( out.text.size( ) >= maxLength )
			break;

		out.text.insert( out.caret, 1, text[i] );
		++out.caret;
	}

	out.anchor = out.caret;
	return out;
}

TextInput Backspace( const TextInput &in )
{
	if ( HasSelection( in ))
		return DeleteSelection( in );

	TextInput out = NormaliseInput( in );
	if ( out.caret == 0 )
		return out;

	out.text.erase( out.caret - 1, 1 );
	--out.caret;
	out.anchor = out.caret;
	return out;
}

TextInput DeleteForward( const TextInput &in )
{
	if ( HasSelection( in ))
		return DeleteSelection( in );

	TextInput out = NormaliseInput( in );
	if ( out.caret >= out.text.size( ))
		return out;

	out.text.erase( out.caret, 1 );
	out.anchor = out.caret;
	return out;
}

TextInput MoveCaret( const TextInput &in, int delta, bool extend )
{
	TextInput out = NormaliseInput( in );

	// An UNSHIFTED arrow on a selection collapses to the end it was pressed towards, rather than
	// stepping one past wherever the caret happened to be. That is what every text field does, and it
	// is what makes arrowing off a selection land where the eye expects.
	if ( !extend && HasSelection( out ))
	{
		out.caret = ( delta < 0 ) ? SelectionStart( out ) : SelectionEnd( out );
		out.anchor = out.caret;
		return out;
	}

	out.caret = Step( out.caret, delta, out.text.size( ));
	if ( !extend )
		out.anchor = out.caret;

	return out;
}

TextInput CaretHome( const TextInput &in, bool extend )
{
	TextInput out = NormaliseInput( in );
	out.caret = 0;
	if ( !extend )
		out.anchor = 0;
	return out;
}

TextInput CaretEnd( const TextInput &in, bool extend )
{
	TextInput out = NormaliseInput( in );
	out.caret = out.text.size( );
	if ( !extend )
		out.anchor = out.caret;
	return out;
}

namespace
{
bool IsWordChar( char c )
{
	return ( c != ' ' );
}
} // namespace

TextInput MoveWord( const TextInput &in, bool forward, bool extend )
{
	TextInput out = NormaliseInput( in );
	const size_t length = out.text.size( );

	if ( forward )
	{
		// Over the current word, then over the spaces after it -- so repeated presses land at the
		// start of each following word rather than stopping in every gap on the way.
		size_t at = out.caret;
		while (( at < length ) && IsWordChar( out.text[at] ))
			++at;
		while (( at < length ) && !IsWordChar( out.text[at] ))
			++at;
		out.caret = at;
	}
	else
	{
		// The mirror: any spaces immediately behind, then the word behind those.
		size_t at = out.caret;
		while (( at > 0 ) && !IsWordChar( out.text[at - 1] ))
			--at;
		while (( at > 0 ) && IsWordChar( out.text[at - 1] ))
			--at;
		out.caret = at;
	}

	if ( !extend )
		out.anchor = out.caret;

	return out;
}

TextInput SelectWordAt( const TextInput &in, size_t pos )
{
	TextInput out = NormaliseInput( in );
	const size_t length = out.text.size( );

	if (( length == 0 ) || ( pos >= length ) || !IsWordChar( out.text[pos] ))
	{
		// No word here. Collapsing rather than reaching for a neighbouring one: which side would it
		// pick, and why would the player agree?
		out.caret = ( pos > length ) ? length : pos;
		out.anchor = out.caret;
		return out;
	}

	size_t from = pos;
	while (( from > 0 ) && IsWordChar( out.text[from - 1] ))
		--from;

	size_t to = pos;
	while (( to < length ) && IsWordChar( out.text[to] ))
		++to;

	out.anchor = from;
	out.caret = to;
	return out;
}

TextInput ClearInput( )
{
	return TextInput( );
}

TextInput NormaliseInput( const TextInput &in )
{
	TextInput out;
	out.text = in.text;
	out.caret = Clamp( in.caret, in.text.size( ));
	out.anchor = Clamp( in.anchor, in.text.size( ));
	return out;
}

} // namespace zx
