// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/dialog_compute.h"

using zx::ComputeDialogEscape;
using zx::ComputeDialogFocus;
using zx::ComputeDialogShortcut;
using zx::DialogKey;
using std::vector;

namespace
{
const vector<char> kYesNo = { 'y', 'n' };
} // namespace

// ---------------------------------------------------------------- moving between choices

TEST( Dialog, MovesLeftAndRight )
{
	EXPECT_EQ( 1, ComputeDialogFocus( 0, 2, DialogKey::Right ));
	EXPECT_EQ( 0, ComputeDialogFocus( 1, 2, DialogKey::Left ));
}

TEST( Dialog, WrapsAtBothEnds )
{
	// A row of two or three buttons is a ring in the player's head, and stopping dead at the end of
	// it just reads as broken.
	EXPECT_EQ( 0, ComputeDialogFocus( 1, 2, DialogKey::Right ));
	EXPECT_EQ( 1, ComputeDialogFocus( 0, 2, DialogKey::Left ));
	EXPECT_EQ( 2, ComputeDialogFocus( 0, 3, DialogKey::Left ));
}

TEST( Dialog, UpAndDownDoTheSameAsLeftAndRight )
{
	// The buttons sit in a row, but a player who was arrowing DOWN a server list a second ago should
	// not have to notice that the dialog changed axis on them.
	EXPECT_EQ( ComputeDialogFocus( 0, 3, DialogKey::Right ), ComputeDialogFocus( 0, 3, DialogKey::Down ));
	EXPECT_EQ( ComputeDialogFocus( 0, 3, DialogKey::Left ), ComputeDialogFocus( 0, 3, DialogKey::Up ));
}

TEST( Dialog, NormalisesAFocusThatArrivedOutOfRange )
{
	// One bad value must not keep producing bad ones.
	EXPECT_GE( ComputeDialogFocus( -5, 2, DialogKey::Right ), 0 );
	EXPECT_LT( ComputeDialogFocus( -5, 2, DialogKey::Right ), 2 );
	EXPECT_GE( ComputeDialogFocus( 99, 2, DialogKey::Left ), 0 );
	EXPECT_LT( ComputeDialogFocus( 99, 2, DialogKey::Left ), 2 );
}

TEST( Dialog, AlwaysReturnsSomethingSafeToIndexWith )
{
	// Swept, because the return value goes straight into an array subscript.
	const DialogKey keys[] = { DialogKey::Left, DialogKey::Right, DialogKey::Up, DialogKey::Down };

	for ( int count = 0; count <= 4; ++count )
		for ( int focus = -3; focus <= 6; ++focus )
			for ( int k = 0; k < 4; ++k )
			{
				const int at = ComputeDialogFocus( focus, count, keys[k] );
				EXPECT_GE( at, 0 ) << count << "," << focus;
				if ( count > 0 )
					EXPECT_LT( at, count ) << count << "," << focus;
			}
}

TEST( Dialog, AnEmptyDialogHasNowhereToGo )
{
	EXPECT_EQ( 0, ComputeDialogFocus( 0, 0, DialogKey::Right ));
}

TEST( Dialog, ASingleChoiceStaysOnItself )
{
	EXPECT_EQ( 0, ComputeDialogFocus( 0, 1, DialogKey::Right ));
	EXPECT_EQ( 0, ComputeDialogFocus( 0, 1, DialogKey::Left ));
}

// ---------------------------------------------------------------- shortcuts

TEST( Dialog, PicksTheChoiceWhoseLetterWasTyped )
{
	EXPECT_EQ( 0, ComputeDialogShortcut( kYesNo, 'y' ));
	EXPECT_EQ( 1, ComputeDialogShortcut( kYesNo, 'n' ));
}

TEST( Dialog, ShortcutsIgnoreCase )
{
	// A player holding shift is still answering the question.
	EXPECT_EQ( 0, ComputeDialogShortcut( kYesNo, 'Y' ));
	EXPECT_EQ( 1, ComputeDialogShortcut( kYesNo, 'N' ));
}

TEST( Dialog, AnUnrelatedKeyPicksNothing )
{
	EXPECT_EQ( -1, ComputeDialogShortcut( kYesNo, 'q' ));
	EXPECT_EQ( -1, ComputeDialogShortcut( kYesNo, ' ' ));
	EXPECT_EQ( -1, ComputeDialogShortcut( vector<char>( ), 'y' ));
}

TEST( Dialog, AChoiceWithNoShortcutIsSkipped )
{
	// Reachable by arrow and by mouse; it simply has no letter. A zero must never match the NUL that
	// a stray key event can carry.
	const vector<char> mixed = { 0, 'n' };

	EXPECT_EQ( -1, ComputeDialogShortcut( mixed, 0 ));
	EXPECT_EQ( 1, ComputeDialogShortcut( mixed, 'n' ));
}

TEST( Dialog, TheFirstMatchWins )
{
	const vector<char> duplicated = { 'y', 'y' };
	EXPECT_EQ( 0, ComputeDialogShortcut( duplicated, 'y' ));
}

// ---------------------------------------------------------------- backing out

TEST( Dialog, EscapeResolvesToTheDeclaredSafeChoice )
{
	// NOT to whatever has focus. Escape means "I did not want this", and a dialog where backing out
	// could confirm something destructive depending on where the highlight had drifted is a trap.
	EXPECT_EQ( 1, ComputeDialogEscape( 1, 2 ));
	EXPECT_EQ( 0, ComputeDialogEscape( 0, 2 ));
}

TEST( Dialog, EscapeDoesNothingWhenThereIsNoSafeChoice )
{
	// Better than guessing. A dialog with no way out is a bug the caller has to fix; picking one at
	// random would hide it behind a destructive answer.
	EXPECT_EQ( -1, ComputeDialogEscape( -1, 2 ));
	EXPECT_EQ( -1, ComputeDialogEscape( 5, 2 ));
	EXPECT_EQ( -1, ComputeDialogEscape( 0, 0 ));
}
