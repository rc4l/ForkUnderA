// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/textinput_compute.h"

using zx::Backspace;
using zx::CaretEnd;
using zx::CaretHome;
using zx::DeleteForward;
using zx::InsertChar;
using zx::IsTypable;
using zx::MoveCaret;
using zx::NormaliseInput;
using zx::TextInput;

namespace
{
const size_t kPlenty = 64;

// Types a whole string in, one character at a time, exactly as the key handler will.
TextInput Type( const char *s, size_t maxLength = kPlenty )
{
	TextInput in;
	for ( const char *p = s; *p != 0; ++p )
		in = InsertChar( in, *p, maxLength );
	return in;
}
} // namespace

// ---------------------------------------------------------------- what may be typed

TEST( TextInput, AcceptsPrintableAsciiIncludingSpace )
{
	EXPECT_TRUE( IsTypable( ' ' ));
	EXPECT_TRUE( IsTypable( 'a' ));
	EXPECT_TRUE( IsTypable( 'Z' ));
	EXPECT_TRUE( IsTypable( '9' ));
	EXPECT_TRUE( IsTypable( '~' ));
}

TEST( TextInput, RefusesControlCharactersAndEverythingAboveAscii )
{
	// The fonts have no glyphs for these, and a password quietly holding a character the server
	// cannot receive is worse than one that refuses it at the keyboard.
	EXPECT_FALSE( IsTypable( 0 ));
	EXPECT_FALSE( IsTypable( '\n' ));
	EXPECT_FALSE( IsTypable( '\t' ));
	EXPECT_FALSE( IsTypable( 27 ));		// escape
	EXPECT_FALSE( IsTypable( 127 ));	// delete
	EXPECT_FALSE( IsTypable( 200 ));
	EXPECT_FALSE( IsTypable( -1 ));		// what a signed char sign-extends to
}

TEST( TextInput, TypingARefusedCharacterChangesNothing )
{
	const TextInput before = Type( "ab" );
	const TextInput after = InsertChar( before, '\n', kPlenty );

	EXPECT_EQ( before.text, after.text );
	EXPECT_EQ( before.caret, after.caret );
}

// ---------------------------------------------------------------- typing

TEST( TextInput, TypingAppendsAndMovesTheCaretWithIt )
{
	const TextInput in = Type( "doom" );
	EXPECT_EQ( "doom", in.text );
	EXPECT_EQ( 4u, in.caret );
}

TEST( TextInput, TypingInsertsAtTheCaretRatherThanAtTheEnd )
{
	TextInput in = Type( "dom" );
	in = MoveCaret( in, -1 );			// between 'o' and 'm'
	in = InsertChar( in, 'o', kPlenty );

	EXPECT_EQ( "doom", in.text );
	EXPECT_EQ( 3u, in.caret );			// stepped over what was inserted
}

TEST( TextInput, StopsAtTheLengthLimit )
{
	const TextInput in = Type( "abcdef", 3 );
	EXPECT_EQ( "abc", in.text );
	EXPECT_EQ( 3u, in.caret );
}

TEST( TextInput, ALimitOfZeroAcceptsNothing )
{
	const TextInput in = Type( "abc", 0 );
	EXPECT_EQ( "", in.text );
	EXPECT_EQ( 0u, in.caret );
}

// ---------------------------------------------------------------- erasing

TEST( TextInput, BackspaceErasesBeforeTheCaret )
{
	const TextInput in = Backspace( Type( "doom" ));
	EXPECT_EQ( "doo", in.text );
	EXPECT_EQ( 3u, in.caret );
}

TEST( TextInput, BackspaceAtTheStartDoesNothing )
{
	const TextInput in = Backspace( CaretHome( Type( "doom" )));
	EXPECT_EQ( "doom", in.text );
	EXPECT_EQ( 0u, in.caret );
}

TEST( TextInput, BackspaceOnAnEmptyFieldDoesNothing )
{
	const TextInput in = Backspace( TextInput( ));
	EXPECT_EQ( "", in.text );
	EXPECT_EQ( 0u, in.caret );
}

TEST( TextInput, DeleteErasesAtTheCaretAndLeavesItPut )
{
	const TextInput in = DeleteForward( CaretHome( Type( "doom" )));
	EXPECT_EQ( "oom", in.text );
	EXPECT_EQ( 0u, in.caret );
}

TEST( TextInput, DeleteAtTheEndDoesNothing )
{
	const TextInput in = DeleteForward( Type( "doom" ));
	EXPECT_EQ( "doom", in.text );
	EXPECT_EQ( 4u, in.caret );
}

// ---------------------------------------------------------------- the caret

TEST( TextInput, MovesWithinTheTextAndStopsAtBothEnds )
{
	const TextInput in = Type( "doom" );

	EXPECT_EQ( 3u, MoveCaret( in, -1 ).caret );
	EXPECT_EQ( 0u, MoveCaret( in, -4 ).caret );
	EXPECT_EQ( 0u, MoveCaret( in, -99 ).caret );		// clamped, not wrapped
	EXPECT_EQ( 4u, MoveCaret( CaretHome( in ), 99 ).caret );
}

TEST( TextInput, MovingNeverChangesTheText )
{
	const TextInput in = Type( "doom" );
	EXPECT_EQ( "doom", MoveCaret( in, -2 ).text );
	EXPECT_EQ( "doom", CaretHome( in ).text );
	EXPECT_EQ( "doom", CaretEnd( in ).text );
}

TEST( TextInput, AHugeForwardDeltaDoesNotWrapPastTheEnd )
{
	// caret + delta overflows if it is added before it is checked, and a caret before the start of
	// the string is a crash waiting for the next keystroke.
	const TextInput in = MoveCaret( CaretHome( Type( "doom" )), 2000000000 );
	EXPECT_EQ( 4u, in.caret );
}

TEST( TextInput, HomeAndEndGoWhereTheySay )
{
	const TextInput in = Type( "doom" );
	EXPECT_EQ( 0u, CaretHome( in ).caret );
	EXPECT_EQ( 4u, CaretEnd( in ).caret );
	EXPECT_EQ( 0u, CaretEnd( TextInput( )).caret );
}

// ---------------------------------------------------------------- state from outside

TEST( TextInput, NormalisesACaretPastTheEnd )
{
	// Text can arrive from outside the field -- a remembered query, a pre-filled password -- and its
	// caret cannot be trusted to belong to it.
	const TextInput in = NormaliseInput( TextInput( "ab", 99 ));
	EXPECT_EQ( "ab", in.text );
	EXPECT_EQ( 2u, in.caret );
}

TEST( TextInput, EveryOperationLeavesTheCaretInsideTheText )
{
	// The invariant the whole unit rests on, swept rather than spot-checked: the caret is a position
	// BETWEEN characters, so [0, length] inclusive, whatever it was handed.
	const TextInput starts[] = {
		TextInput( ), TextInput( "a", 0 ), TextInput( "a", 1 ), TextInput( "abc", 2 ),
		TextInput( "abc", 99 ),			// nonsense, on purpose
	};

	for ( size_t i = 0; i < sizeof( starts ) / sizeof( starts[0] ); ++i )
	{
		const TextInput ops[] = {
			InsertChar( starts[i], 'x', kPlenty ),
			InsertChar( starts[i], 'x', 0 ),
			Backspace( starts[i] ),
			DeleteForward( starts[i] ),
			MoveCaret( starts[i], -5 ),
			MoveCaret( starts[i], 5 ),
			CaretHome( starts[i] ),
			CaretEnd( starts[i] ),
			NormaliseInput( starts[i] ),
		};

		for ( size_t k = 0; k < sizeof( ops ) / sizeof( ops[0] ); ++k )
			EXPECT_LE( ops[k].caret, ops[k].text.size( )) << i << "," << k;
	}
}

TEST( TextInput, TypingThenErasingEverythingComesBackEmpty )
{
	TextInput in = Type( "a longer query" );
	for ( int i = 0; i < 100; ++i )
		in = Backspace( in );

	EXPECT_EQ( "", in.text );
	EXPECT_EQ( 0u, in.caret );
}
