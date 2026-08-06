// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/textinput_compute.h"

using zx::Backspace;
using zx::CaretEnd;
using zx::CaretHome;
using zx::ClearInput;
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
	in = MoveCaret( in, -1, false );			// between 'o' and 'm'
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
	const TextInput in = Backspace( CaretHome( Type( "doom" ), false ));
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
	const TextInput in = DeleteForward( CaretHome( Type( "doom" ), false ));
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

	EXPECT_EQ( 3u, MoveCaret( in, -1, false ).caret );
	EXPECT_EQ( 0u, MoveCaret( in, -4, false ).caret );
	EXPECT_EQ( 0u, MoveCaret( in, -99, false ).caret );		// clamped, not wrapped
	EXPECT_EQ( 4u, MoveCaret( CaretHome( in, false ), 99, false ).caret );
}

TEST( TextInput, MovingNeverChangesTheText )
{
	const TextInput in = Type( "doom" );
	EXPECT_EQ( "doom", MoveCaret( in, -2, false ).text );
	EXPECT_EQ( "doom", CaretHome( in, false ).text );
	EXPECT_EQ( "doom", CaretEnd( in, false ).text );
}

TEST( TextInput, AHugeForwardDeltaDoesNotWrapPastTheEnd )
{
	// caret + delta overflows if it is added before it is checked, and a caret before the start of
	// the string is a crash waiting for the next keystroke.
	const TextInput in = MoveCaret( CaretHome( Type( "doom" ), false ), 2000000000, false );
	EXPECT_EQ( 4u, in.caret );
}

TEST( TextInput, HomeAndEndGoWhereTheySay )
{
	const TextInput in = Type( "doom" );
	EXPECT_EQ( 0u, CaretHome( in, false ).caret );
	EXPECT_EQ( 4u, CaretEnd( in, false ).caret );
	EXPECT_EQ( 0u, CaretEnd( TextInput( ), false ).caret );
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
			MoveCaret( starts[i], -5, false ),
			MoveCaret( starts[i], 5, true ),
			CaretHome( starts[i], false ),
			CaretEnd( starts[i], true ),
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

// ---------------------------------------------------------------- selection

TEST( TextInput, HasNoSelectionUntilOneIsMade )
{
	EXPECT_FALSE( zx::HasSelection( Type( "doom" )));
	EXPECT_FALSE( zx::HasSelection( TextInput( )));
	EXPECT_EQ( "", zx::SelectedText( Type( "doom" )));
}

TEST( TextInput, SelectAllTakesTheWholeLine )
{
	const TextInput in = zx::SelectAll( Type( "doom" ));

	EXPECT_TRUE( zx::HasSelection( in ));
	EXPECT_EQ( "doom", zx::SelectedText( in ));
	EXPECT_EQ( 4u, in.caret );			// where every text field leaves it
}

TEST( TextInput, SelectAllOnAnEmptyFieldSelectsNothing )
{
	EXPECT_FALSE( zx::HasSelection( zx::SelectAll( TextInput( ))));
}

TEST( TextInput, TheRangeIsOrderedWhicheverWayItWasDragged )
{
	// Dragged right, then dragged left. Both describe the same two characters, and every caller wants
	// them the same way round.
	const TextInput rightwards = zx::SetCaret( zx::SetCaret( Type( "doom" ), 1, false ), 3, true );
	const TextInput leftwards = zx::SetCaret( zx::SetCaret( Type( "doom" ), 3, false ), 1, true );

	EXPECT_EQ( "oo", zx::SelectedText( rightwards ));
	EXPECT_EQ( "oo", zx::SelectedText( leftwards ));
	EXPECT_EQ( zx::SelectionStart( rightwards ), zx::SelectionStart( leftwards ));
	EXPECT_EQ( zx::SelectionEnd( rightwards ), zx::SelectionEnd( leftwards ));
}

TEST( TextInput, DraggingExtendsAndClickingCollapses )
{
	// A drag is SetCaret with extend; a plain click is SetCaret without, which is what puts the caret
	// somewhere and drops whatever was selected.
	const TextInput dragged = zx::SetCaret( zx::SetCaret( Type( "doom" ), 0, false ), 2, true );
	EXPECT_EQ( "do", zx::SelectedText( dragged ));

	const TextInput clicked = zx::SetCaret( dragged, 3, false );
	EXPECT_FALSE( zx::HasSelection( clicked ));
	EXPECT_EQ( 3u, clicked.caret );
}

TEST( TextInput, ShiftArrowsGrowTheSelectionAndPlainOnesDropIt )
{
	const TextInput start = Type( "doom" );

	const TextInput extended = MoveCaret( MoveCaret( start, -1, true ), -1, true );
	EXPECT_EQ( "om", zx::SelectedText( extended ));

	EXPECT_FALSE( zx::HasSelection( MoveCaret( extended, -1, false )));
}

TEST( TextInput, AnUnshiftedArrowCollapsesToTheEndItWasPressedTowards )
{
	// Not one past wherever the caret happened to be. This is what every text field does and what
	// makes arrowing off a selection land where the eye expects.
	const TextInput selected = zx::SelectAll( Type( "doom" ));

	EXPECT_EQ( 0u, MoveCaret( selected, -1, false ).caret );
	EXPECT_EQ( 4u, MoveCaret( selected, 1, false ).caret );
}

TEST( TextInput, ShiftHomeAndEndExtendToTheEnds )
{
	const TextInput in = zx::SetCaret( Type( "doom" ), 2, false );

	EXPECT_EQ( "do", zx::SelectedText( CaretHome( in, true )));
	EXPECT_EQ( "om", zx::SelectedText( CaretEnd( in, true )));
	EXPECT_FALSE( zx::HasSelection( CaretHome( in, false )));
}

// ---------------------------------------------------------------- editing over a selection

TEST( TextInput, TypingReplacesTheSelection )
{
	// The one behaviour every text field shares and the one people notice instantly when missing.
	const TextInput in = InsertChar( zx::SelectAll( Type( "doom" )), 'x', kPlenty );

	EXPECT_EQ( "x", in.text );
	EXPECT_EQ( 1u, in.caret );
	EXPECT_FALSE( zx::HasSelection( in ));
}

TEST( TextInput, BackspaceAndDeleteBothEraseTheSelection )
{
	const TextInput selected = zx::SetCaret( zx::SetCaret( Type( "doom" ), 1, false ), 3, true );

	EXPECT_EQ( "dm", Backspace( selected ).text );
	EXPECT_EQ( "dm", DeleteForward( selected ).text );
	EXPECT_EQ( 1u, Backspace( selected ).caret );		// where the range began, either way
	EXPECT_EQ( 1u, DeleteForward( selected ).caret );
}

TEST( TextInput, TypingOverAFullFieldStillReplacesTheSelection )
{
	// The length check must come AFTER the selection is erased, or selecting everything in a full
	// field and typing would do nothing at all.
	const TextInput full = Type( "abc", 3 );
	const TextInput in = InsertChar( zx::SelectAll( full ), 'x', 3 );

	EXPECT_EQ( "x", in.text );
}

// ---------------------------------------------------------------- paste

TEST( TextInput, PasteInsertsAtTheCaret )
{
	const TextInput in = zx::InsertText( zx::SetCaret( Type( "dm" ), 1, false ), "oo", kPlenty );
	EXPECT_EQ( "doom", in.text );
	EXPECT_EQ( 3u, in.caret );
}

TEST( TextInput, PasteReplacesTheSelection )
{
	const TextInput in = zx::InsertText( zx::SelectAll( Type( "doom" )), "quake", kPlenty );
	EXPECT_EQ( "quake", in.text );
	EXPECT_FALSE( zx::HasSelection( in ));
}

TEST( TextInput, PasteDropsWhatItCannotTypeRatherThanRefusingOutright )
{
	// A clipboard routinely carries a trailing newline. Rejecting the whole paste over one would be a
	// puzzle rather than a safeguard.
	EXPECT_EQ( "brutal", zx::InsertText( TextInput( ), "brutal\n", kPlenty ).text );
	EXPECT_EQ( "ab", zx::InsertText( TextInput( ), "a\tb", kPlenty ).text );
}

TEST( TextInput, PasteStopsAtTheLengthLimit )
{
	const TextInput in = zx::InsertText( TextInput( ), "abcdefgh", 3 );
	EXPECT_EQ( "abc", in.text );
	EXPECT_EQ( 3u, in.caret );
}

TEST( TextInput, PastingNothingChangesNothing )
{
	const TextInput before = Type( "doom" );
	const TextInput after = zx::InsertText( before, "", kPlenty );

	EXPECT_EQ( before.text, after.text );
	EXPECT_EQ( before.caret, after.caret );
}

TEST( TextInput, EveryOperationLeavesBothEndsOfTheSelectionInsideTheText )
{
	// The caret invariant again, now that there are two positions that have to hold it. A stale
	// anchor is exactly as much of a crash as a stale caret, and much easier to forget.
	const TextInput starts[] = {
		TextInput( ), TextInput( "abc", 0, 0 ), TextInput( "abc", 3, 0 ),
		TextInput( "abc", 1, 2 ), TextInput( "abc", 99, 99 ), TextInput( "abc", 0, 99 ),
	};

	for ( size_t i = 0; i < sizeof( starts ) / sizeof( starts[0] ); ++i )
	{
		const TextInput ops[] = {
			InsertChar( starts[i], 'x', kPlenty ),
			zx::InsertText( starts[i], "xy", kPlenty ),
			Backspace( starts[i] ),
			DeleteForward( starts[i] ),
			zx::DeleteSelection( starts[i] ),
			zx::SelectAll( starts[i] ),
			zx::SetCaret( starts[i], 99, true ),
			zx::SetCaret( starts[i], 1, false ),
			MoveCaret( starts[i], -5, true ),
			MoveCaret( starts[i], 5, false ),
			CaretHome( starts[i], true ),
			CaretEnd( starts[i], false ),
		};

		for ( size_t k = 0; k < sizeof( ops ) / sizeof( ops[0] ); ++k )
		{
			EXPECT_LE( ops[k].caret, ops[k].text.size( )) << i << "," << k;
			EXPECT_LE( ops[k].anchor, ops[k].text.size( )) << i << "," << k;
		}
	}
}

// ---------------------------------------------------------------- word movement

TEST( TextInput, CtrlLeftGoesToTheStartOfTheWordBehind )
{
	const TextInput in = Type( "brutal doom" );

	const TextInput once = zx::MoveWord( in, false, false );
	EXPECT_EQ( 7u, once.caret );			// before "doom"

	const TextInput twice = zx::MoveWord( once, false, false );
	EXPECT_EQ( 0u, twice.caret );			// before "brutal"
}

TEST( TextInput, CtrlRightGoesPastTheWordAheadAndItsSpaces )
{
	const TextInput in = CaretHome( Type( "brutal doom" ), false );

	const TextInput once = zx::MoveWord( in, true, false );
	EXPECT_EQ( 7u, once.caret );			// over "brutal" and the space after it

	const TextInput twice = zx::MoveWord( once, true, false );
	EXPECT_EQ( 11u, twice.caret );
}

TEST( TextInput, WordMovementNeverStopsInAGap )
{
	// The edge these always get wrong: repeated presses must land at the start of each word, not in
	// the space between them.
	TextInput in = CaretHome( Type( "a  bb   ccc" ), false );

	in = zx::MoveWord( in, true, false );
	EXPECT_EQ( 'b', in.text[in.caret] );

	in = zx::MoveWord( in, true, false );
	EXPECT_EQ( 'c', in.text[in.caret] );
}

TEST( TextInput, WordMovementStopsAtBothEnds )
{
	EXPECT_EQ( 0u, zx::MoveWord( CaretHome( Type( "doom" ), false ), false, false ).caret );
	EXPECT_EQ( 4u, zx::MoveWord( Type( "doom" ), true, false ).caret );
	EXPECT_EQ( 0u, zx::MoveWord( TextInput( ), false, false ).caret );
	EXPECT_EQ( 0u, zx::MoveWord( TextInput( ), true, false ).caret );
}

TEST( TextInput, ShiftCtrlArrowSelectsByWord )
{
	const TextInput in = zx::MoveWord( Type( "brutal doom" ), false, true );
	EXPECT_EQ( "doom", zx::SelectedText( in ));
}

// ---------------------------------------------------------------- double-click

TEST( TextInput, DoubleClickTakesTheWholeWordUnderThePointer )
{
	const TextInput in = Type( "brutal doom" );

	EXPECT_EQ( "brutal", zx::SelectedText( zx::SelectWordAt( in, 3 )));
	EXPECT_EQ( "doom", zx::SelectedText( zx::SelectWordAt( in, 8 )));
	EXPECT_EQ( "brutal", zx::SelectedText( zx::SelectWordAt( in, 0 )));
	EXPECT_EQ( "doom", zx::SelectedText( zx::SelectWordAt( in, 10 )));
}

TEST( TextInput, DoubleClickOnASpaceSelectsNothing )
{
	// Reaching for a neighbouring word would be a guess -- which side, and why would the player
	// agree?
	EXPECT_FALSE( zx::HasSelection( zx::SelectWordAt( Type( "brutal doom" ), 6 )));
	EXPECT_FALSE( zx::HasSelection( zx::SelectWordAt( Type( "brutal doom" ), 99 )));
	EXPECT_FALSE( zx::HasSelection( zx::SelectWordAt( TextInput( ), 0 )));
}

TEST( TextInput, WordOperationsKeepBothEndsInsideTheText )
{
	const TextInput starts[] = {
		TextInput( ), TextInput( "a b", 0 ), TextInput( "a b", 3 ), TextInput( "  ", 1 ),
		TextInput( "abc", 99, 99 ),
	};

	for ( size_t i = 0; i < sizeof( starts ) / sizeof( starts[0] ); ++i )
	{
		const TextInput ops[] = {
			zx::MoveWord( starts[i], true, false ),
			zx::MoveWord( starts[i], false, true ),
			zx::SelectWordAt( starts[i], 0 ),
			zx::SelectWordAt( starts[i], 99 ),
		};

		for ( size_t k = 0; k < sizeof( ops ) / sizeof( ops[0] ); ++k )
		{
			EXPECT_LE( ops[k].caret, ops[k].text.size( )) << i << "," << k;
			EXPECT_LE( ops[k].anchor, ops[k].text.size( )) << i << "," << k;
		}
	}
}

TEST( TextInput, DoubleClickPastTheEndOfTheTextSelectsEverything )
{
	// The blank part of the box. Selecting nothing there would be technically defensible and useless:
	// a double-click is a request to grab something.
	const TextInput in = Type( "brutal doom" );

	EXPECT_EQ( "brutal doom", zx::SelectedText( zx::SelectWordOrAll( in, 11 )));
	EXPECT_EQ( "brutal doom", zx::SelectedText( zx::SelectWordOrAll( in, 99 )));
}

TEST( TextInput, DoubleClickInAGapSelectsEverythingToo )
{
	EXPECT_EQ( "brutal doom", zx::SelectedText( zx::SelectWordOrAll( Type( "brutal doom" ), 6 )));
}

TEST( TextInput, DoubleClickOnAWordStillTakesJustThatWord )
{
	const TextInput in = Type( "brutal doom" );
	EXPECT_EQ( "brutal", zx::SelectedText( zx::SelectWordOrAll( in, 2 )));
	EXPECT_EQ( "doom", zx::SelectedText( zx::SelectWordOrAll( in, 9 )));
}

TEST( TextInput, DoubleClickInAnEmptyBoxSelectsNothingBecauseThereIsNothing )
{
	EXPECT_FALSE( zx::HasSelection( zx::SelectWordOrAll( TextInput( ), 0 )));
}

TEST( TextInput, ClearingGivesBackAnEmptyFieldWithNoSelection )
{
	// Used whenever a field is opened or reused -- a dialog put up twice must not present the last
	// answer, and a leftover anchor would make the first keystroke overwrite text that is not there.
	const TextInput cleared = ClearInput( );

	EXPECT_TRUE( cleared.text.empty( ));
	EXPECT_EQ( 0u, cleared.caret );
	EXPECT_EQ( 0u, cleared.anchor );
}
