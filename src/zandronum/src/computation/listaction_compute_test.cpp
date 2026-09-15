// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "computation/listaction_compute.h"

using namespace zx;

namespace
{

ListActionStep FromList( ListActionKey key, bool actionAvailable = true )
{
	return StepListAction( ListActionZone::List, key, actionAvailable );
}

ListActionStep FromAction( ListActionKey key )
{
	return StepListAction( ListActionZone::Action, key, true );
}

} // namespace

TEST( ListAction, EnterOnARowMovesToTheButtonRatherThanActing )
{
	// The whole point of the pair: a list that launched under the player's hands gave them nowhere
	// to read what a row was before committing to it.
	const ListActionStep step = FromList( ListActionKey::Enter );

	EXPECT_EQ( ListActionZone::Action, step.zone );
	EXPECT_FALSE( step.activate );
	EXPECT_EQ( 0, step.rowStep );
}

TEST( ListAction, RightReachesTheButtonToo )
{
	EXPECT_EQ( ListActionZone::Action, FromList( ListActionKey::Right ).zone );
}

TEST( ListAction, WithNoButtonTheFocusStaysInTheList )
{
	// A focus you cannot see is a menu the player has lost.
	EXPECT_EQ( ListActionZone::List, FromList( ListActionKey::Right, false ).zone );
	EXPECT_EQ( ListActionZone::List, FromList( ListActionKey::Enter, false ).zone );
	EXPECT_FALSE( FromList( ListActionKey::Enter, false ).activate );
}

TEST( ListAction, TheArrowsWalkTheListWithoutLeavingIt )
{
	EXPECT_EQ( -1, FromList( ListActionKey::Up ).rowStep );
	EXPECT_EQ( 1, FromList( ListActionKey::Down ).rowStep );
	EXPECT_EQ( ListActionZone::List, FromList( ListActionKey::Up ).zone );
	EXPECT_EQ( ListActionZone::List, FromList( ListActionKey::Down ).zone );
}

TEST( ListAction, LeftOutOfTheListIsTheCallersProblem )
{
	// There is nothing that way in either menu, and wrapping round to the button would make the two
	// horizontal keys disagree about which way the layout runs.
	EXPECT_EQ( ListActionZone::Outside, FromList( ListActionKey::Left ).zone );
}

TEST( ListAction, LeftComesBackFromTheButton )
{
	const ListActionStep step = FromAction( ListActionKey::Left );

	EXPECT_EQ( ListActionZone::List, step.zone );
	EXPECT_EQ( 0, step.rowStep );
	EXPECT_FALSE( step.activate );
}

TEST( ListAction, DownFromTheButtonGoesToTheNextRow )
{
	// The button sits beside a row, so the next thing below it is the next row.
	const ListActionStep step = FromAction( ListActionKey::Down );

	EXPECT_EQ( ListActionZone::List, step.zone );
	EXPECT_EQ( 1, step.rowStep );
}

TEST( ListAction, EnterOnTheButtonIsTheOnlyThingThatActs )
{
	const ListActionStep step = FromAction( ListActionKey::Enter );

	EXPECT_TRUE( step.activate );
	EXPECT_EQ( ListActionZone::Action, step.zone );
}

TEST( ListAction, NothingElseActs )
{
	// Every key from every zone, and exactly one combination may return activate.
	const ListActionKey keys[] = { ListActionKey::Up, ListActionKey::Down, ListActionKey::Left,
		ListActionKey::Right, ListActionKey::Enter };

	for ( int z = 0; z <= 1; ++z )
	{
		const ListActionZone zone = ( z == 0 ) ? ListActionZone::List : ListActionZone::Action;

		for ( int k = 0; k < 5; ++k )
		{
			const bool bShouldAct = ( zone == ListActionZone::Action )
				&& ( keys[k] == ListActionKey::Enter );

			EXPECT_EQ( bShouldAct, StepListAction( zone, keys[k], true ).activate )
				<< "zone " << z << " key " << k;
		}
	}
}

TEST( ListAction, UpFromTheButtonLeavesThePairToTheCaller )
{
	// One menu has a filter row up there and the other has nothing. That is layout, and layout
	// belongs to the menu that has it.
	EXPECT_EQ( ListActionZone::Outside, FromAction( ListActionKey::Up ).zone );
}

TEST( ListAction, TheButtonNeverMovesTheListWhileStayingOnIt )
{
	// Anything that keeps the focus on the button must leave the selection alone, or pressing it
	// afterwards would act on a row the player did not point at.
	const ListActionKey keys[] = { ListActionKey::Up, ListActionKey::Right, ListActionKey::Enter };

	for ( int k = 0; k < 3; ++k )
	{
		const ListActionStep step = FromAction( keys[k] );
		if ( step.zone == ListActionZone::Action )
			EXPECT_EQ( 0, step.rowStep ) << "key " << k;
	}
}
