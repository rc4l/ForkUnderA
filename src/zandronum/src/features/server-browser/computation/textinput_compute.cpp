// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/textinput_compute.h"

namespace zx
{

namespace
{
size_t ClampCaret( size_t caret, size_t length )
{
	return ( caret > length ) ? length : caret;
}
} // namespace

bool IsTypable( int ch )
{
	// Printable 7-bit ASCII, space included. Deliberately not isprint(): that is locale-dependent,
	// and a field whose accepted characters change with the machine's locale is not a field anyone
	// can reason about.
	return ( ch >= 32 ) && ( ch < 127 );
}

TextInput InsertChar( const TextInput &in, int ch, size_t maxLength )
{
	TextInput out = NormaliseInput( in );

	if ( !IsTypable( ch ))
		return out;
	if ( out.text.size( ) >= maxLength )
		return out;

	out.text.insert( out.caret, 1, static_cast<char>( ch ));
	++out.caret;
	return out;
}

TextInput Backspace( const TextInput &in )
{
	TextInput out = NormaliseInput( in );

	if ( out.caret == 0 )
		return out;

	out.text.erase( out.caret - 1, 1 );
	--out.caret;
	return out;
}

TextInput DeleteForward( const TextInput &in )
{
	TextInput out = NormaliseInput( in );

	if ( out.caret >= out.text.size( ))
		return out;

	out.text.erase( out.caret, 1 );
	return out;
}

TextInput MoveCaret( const TextInput &in, int delta )
{
	TextInput out = NormaliseInput( in );

	if ( delta < 0 )
	{
		const size_t back = static_cast<size_t>( -static_cast<long long>( delta ));
		out.caret = ( back >= out.caret ) ? 0 : ( out.caret - back );
	}
	else
	{
		const size_t forward = static_cast<size_t>( delta );

		// Checked as a subtraction rather than an addition: caret + forward can wrap on a large
		// delta, and a caret that lands before the start of the string is a crash waiting for input.
		out.caret = ( forward > ( out.text.size( ) - out.caret ))
			? out.text.size( )
			: ( out.caret + forward );
	}

	return out;
}

TextInput CaretHome( const TextInput &in )
{
	TextInput out = NormaliseInput( in );
	out.caret = 0;
	return out;
}

TextInput CaretEnd( const TextInput &in )
{
	TextInput out = NormaliseInput( in );
	out.caret = out.text.size( );
	return out;
}

TextInput ClearInput( )
{
	return TextInput( );
}

TextInput NormaliseInput( const TextInput &in )
{
	return TextInput( in.text, ClampCaret( in.caret, in.text.size( )));
}

} // namespace zx
