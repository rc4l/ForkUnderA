// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-hosting/computation/hostfocus_compute.h"

using zx::ClampHostFocus;
using zx::ComputeHostNav;
using zx::HostFocusPos;
using zx::HostNavKey;
using zx::HostNavResult;
using zx::HostSlot;

namespace
{

// Four boxes, the settings open, the toggle beside the action -- the ordinary form.
const int kFields = 4;

HostNavResult Nav(HostFocusPos pos, HostNavKey key, bool hasFields = true, bool hasToggle = true)
{
	return ComputeHostNav(pos, key, kFields, hasFields, hasToggle, 0, false);
}

HostFocusPos Field(int i) { return HostFocusPos(HostSlot::Field, i); }
HostFocusPos Vis() { return HostFocusPos(HostSlot::Visibility, 0); }
HostFocusPos Action() { return HostFocusPos(HostSlot::Action, 0); }
HostFocusPos Toggle() { return HostFocusPos(HostSlot::Toggle, 0); }
HostFocusPos List() { return HostFocusPos(HostSlot::List, 0); }

} // namespace

// ---------------------------------------------------------------- down the column

TEST(HostFocus, DownWalksTheFieldsThenTheRowThenTheFoot)
{
	HostFocusPos at = Field(0);

	for (int i = 1; i < kFields; ++i)
	{
		at = Nav(at, HostNavKey::Down).pos;
		EXPECT_EQ(HostSlot::Field, at.slot);
		EXPECT_EQ(i, at.field);
	}

	at = Nav(at, HostNavKey::Down).pos;
	EXPECT_EQ(HostSlot::Visibility, at.slot);

	at = Nav(at, HostNavKey::Down).pos;
	EXPECT_EQ(HostSlot::Action, at.slot);
}

TEST(HostFocus, UpRetracesTheSamePath)
{
	EXPECT_EQ(HostSlot::Visibility, Nav(Action(), HostNavKey::Up).pos.slot);

	const HostFocusPos back = Nav(Vis(), HostNavKey::Up).pos;
	EXPECT_EQ(HostSlot::Field, back.slot);
	EXPECT_EQ(kFields - 1, back.field) << "the LAST field, which is the one it came from";

	EXPECT_EQ(2, Nav(Field(3), HostNavKey::Up).pos.field);
}

TEST(HostFocus, UpOffTheFirstFieldReturnsToTheList)
{
	// Not out of the panel: the list is the other half of this screen, and the fields sit beside it.
	EXPECT_EQ(HostSlot::List, Nav(Field(0), HostNavKey::Up).pos.slot);
}

// ---------------------------------------------------------------- the experience list

TEST(HostFocus, TheListMovesTheSelectionRatherThanFocus)
{
	// [rc4l] The gap this closes. The arrows walked the form and never reached the list, so the one
	// choice the screen exists to make could only be made with a mouse.
	const HostNavResult down = Nav(List(), HostNavKey::Down);
	EXPECT_EQ(1, down.rowStep);
	EXPECT_EQ(HostSlot::List, down.pos.slot) << "moving the selection is not moving focus";

	const HostNavResult up = Nav(List(), HostNavKey::Up);
	EXPECT_EQ(-1, up.rowStep);
	EXPECT_EQ(HostSlot::List, up.pos.slot);
}

TEST(HostFocus, RightCrossesFromTheListToTheRightColumn)
{
	const HostNavResult open = Nav(List(), HostNavKey::Right);
	EXPECT_EQ(HostSlot::Field, open.pos.slot);
	EXPECT_EQ(0, open.pos.field);

	// With the settings shut there are no fields to land on, so it reaches the foot instead.
	EXPECT_EQ(HostSlot::Action, Nav(List(), HostNavKey::Right, false, true).pos.slot);
}

TEST(HostFocus, TheListIsTheLeftmostThing)
{
	EXPECT_EQ(HostSlot::List, Nav(List(), HostNavKey::Left).pos.slot);
	EXPECT_EQ(0, Nav(List(), HostNavKey::Left).rowStep);
}

TEST(HostFocus, LeftOffTheActionGoesBackToTheList)
{
	// The same edge the browser has: ACTION left returns to ROWS.
	EXPECT_EQ(HostSlot::List, Nav(Action(), HostNavKey::Left).pos.slot);
}

TEST(HostFocus, AFieldGivingLeftBackLandsWhereTheActionDoes)
{
	// [rc4l] The field decides WHEN left stops being the caret's; this decides WHERE it goes. Pinned
	// together so the two ways out of the right column cannot come to disagree.
	EXPECT_EQ(HostSlot::List, zx::HostLeftOfTheForm().slot);
	EXPECT_EQ(Nav(Action(), HostNavKey::Left).pos.slot, zx::HostLeftOfTheForm().slot);
}

TEST(HostFocus, DownFromTheFootGoesNowhereRatherThanWrapping)
{
	// [rc4l] Not a wrap. Jumping back to the top of a form because you pressed down once more is a
	// surprise, and the foot is where the form was trying to get you anyway.
	EXPECT_EQ(HostSlot::Action, Nav(Action(), HostNavKey::Down).pos.slot);
	EXPECT_EQ(HostSlot::Toggle, Nav(Toggle(), HostNavKey::Down).pos.slot);
}

// ---------------------------------------------------------------- left and right

TEST(HostFocus, LeftAndRightInAFieldBelongToTheCaret)
{
	// [rc4l] A text box that jumped to another control when you tried to move through what you had
	// typed would be unusable. Same rule the browser's search box follows.
	const HostNavResult left = Nav(Field(1), HostNavKey::Left);
	EXPECT_TRUE(left.caret);
	EXPECT_EQ(HostSlot::Field, left.pos.slot);
	EXPECT_EQ(1, left.pos.field) << "focus did not move";

	const HostNavResult right = Nav(Field(1), HostNavKey::Right);
	EXPECT_TRUE(right.caret);
	EXPECT_EQ(1, right.pos.field);
}

TEST(HostFocus, LeftAndRightOnTheVisibilityRowPickTheChoiceWithoutMoving)
{
	const HostNavResult left = Nav(Vis(), HostNavKey::Left);
	EXPECT_EQ(-1, left.choiceStep);
	EXPECT_EQ(HostSlot::Visibility, left.pos.slot) << "movement and traversal are separate answers";

	const HostNavResult right = Nav(Vis(), HostNavKey::Right);
	EXPECT_EQ(1, right.choiceStep);
	EXPECT_EQ(HostSlot::Visibility, right.pos.slot);
}

TEST(HostFocus, TheFootIsARow)
{
	EXPECT_EQ(HostSlot::Toggle, Nav(Action(), HostNavKey::Right).pos.slot);
	EXPECT_EQ(HostSlot::Action, Nav(Toggle(), HostNavKey::Left).pos.slot);

	// The right end does not wrap; the left end is the way back to the list.
	EXPECT_EQ(HostSlot::Toggle, Nav(Toggle(), HostNavKey::Right).pos.slot);
}

TEST(HostFocus, WithNoToggleTheFootIsOneButtonWide)
{
	// While a server runs the action spans the row and there is nothing beside it, so right must not
	// move focus onto something that is not drawn.
	const HostNavResult r = Nav(Action(), HostNavKey::Right, true, false);
	EXPECT_EQ(HostSlot::Action, r.pos.slot);
}

// ---------------------------------------------------------------- what is not on screen

TEST(HostFocus, WithTheSettingsShutThereAreNoFieldsToWalk)
{
	// [rc4l] The bug this half exists for. The fields and the visibility row are neither drawn nor
	// clickable with the settings closed, and the keyboard used to walk all five of them anyway --
	// five invisible stops between the list and the button.
	EXPECT_EQ(HostSlot::List, Nav(Action(), HostNavKey::Up, false, true).pos.slot);

	// And a stale field focus is corrected rather than honoured.
	const HostNavResult r = Nav(Field(2), HostNavKey::Down, false, true);
	EXPECT_EQ(HostSlot::Action, r.pos.slot);
}

TEST(HostFocus, ClampCorrectsWhatCannotBeThere)
{
	EXPECT_EQ(HostSlot::Action, ClampHostFocus(Field(0), kFields, false, true, 0, false).slot);
	EXPECT_EQ(HostSlot::Action, ClampHostFocus(Vis(), kFields, false, true, 0, false).slot);
	EXPECT_EQ(HostSlot::Action, ClampHostFocus(Toggle(), kFields, true, false, 0, false).slot);

	// A field index past the end is the same kind of stale.
	EXPECT_EQ(0, ClampHostFocus(Field(99), kFields, true, true, 0, false).field);
	EXPECT_EQ(0, ClampHostFocus(Field(-3), kFields, true, true, 0, false).field);

	// Valid positions are left exactly alone.
	EXPECT_EQ(HostSlot::Field, ClampHostFocus(Field(2), kFields, true, true, 0, false).slot);
	EXPECT_EQ(2, ClampHostFocus(Field(2), kFields, true, true, 0, false).field);
	EXPECT_EQ(HostSlot::Toggle, ClampHostFocus(Toggle(), kFields, true, true, 0, false).slot);
}

TEST(HostFocus, NoFieldsAtAllStillLeavesSomewhereToStand)
{
	// A form with its boxes counted as zero must not put focus on a field that cannot exist.
	EXPECT_EQ(HostSlot::Action, ClampHostFocus(Field(0), 0, true, true, 0, false).slot);
	EXPECT_EQ(HostSlot::List, ComputeHostNav(Vis(), HostNavKey::Up, 0, true, true, 0, false).pos.slot);
	EXPECT_EQ(HostSlot::List, ComputeHostNav(Action(), HostNavKey::Up, 0, true, true, 0, false).pos.slot);
}

// ---------------------------------------------------------------- coming back

TEST(HostFocus, ComingBackFromAwayLandsOnTheFirstThingThereIs)
{
	const HostFocusPos away(HostSlot::Away, 0);

	// The list, which is what the panel is for -- not a text box halfway down it.
	EXPECT_EQ(HostSlot::List, Nav(away, HostNavKey::Down).pos.slot);
	EXPECT_EQ(HostSlot::List, Nav(away, HostNavKey::Down, false, true).pos.slot);
}

TEST(HostFocus, AwayIgnoresEverythingButDown)
{
	const HostFocusPos away(HostSlot::Away, 0);

	EXPECT_EQ(HostSlot::Away, Nav(away, HostNavKey::Up).pos.slot);
	EXPECT_EQ(HostSlot::Away, Nav(away, HostNavKey::Left).pos.slot);
	EXPECT_EQ(HostSlot::Away, Nav(away, HostNavKey::Right).pos.slot);
}

// ---------------------------------------------------------------- the invariant itself

TEST(HostFocus, OnePositionCannotNameTwoThings)
{
	// [rc4l] The whole point. This used to be three variables -- a field index, an "on the visibility
	// row" bool and an "on the button" bool -- with an unwritten rule that at most one was set. Four
	// places forgot to clear the others, and the symptoms were a caret in a box that would not type
	// and two things glowing at once.
	//
	// One value cannot disagree with itself, so the rule is now structural rather than remembered.
	const HostNavKey keys[] = {
		HostNavKey::Up, HostNavKey::Down, HostNavKey::Left, HostNavKey::Right,
	};
	const HostFocusPos starts[] = {
		List(), Field(0), Field(2), Field(kFields - 1), Vis(), Action(), Toggle(),
	};

	for (size_t s = 0; s < 7; ++s)
	{
		for (size_t k = 0; k < 4; ++k)
		{
			const HostFocusPos to = Nav(starts[s], keys[k]).pos;

			// Whatever it lands on is one slot, and one that exists.
			EXPECT_EQ(to.slot, ClampHostFocus(to, kFields, true, true, 0, false).slot);

			if (to.slot == HostSlot::Field)
			{
				EXPECT_GE(to.field, 0);
				EXPECT_LT(to.field, kFields);
			}
		}
	}
}

// ------------------------------------------------------------ the gameplay panel

namespace
{

// The panel face: settings shut, so no fields and no visibility row, and `n` rows of controls.
HostNavResult Panel(HostFocusPos pos, HostNavKey key, int n)
{
	return ComputeHostNav(pos, key, kFields, false, true, n, false);
}

HostFocusPos Game(int i)
{
	return HostFocusPos(HostSlot::Gameplay, i);
}

} // namespace

TEST(HostFocus, RightOffTheListEntersTheGameplayPanelWhenTheFormIsShut)
{
	// The form REPLACES the panel, so the right column has one first thing and which it is depends
	// on the face showing. With the settings open this same key goes to the first field.
	EXPECT_EQ(HostSlot::Gameplay, Panel(HostFocusPos(HostSlot::List, 0), HostNavKey::Right, 3).pos.slot);
	EXPECT_EQ(0, Panel(HostFocusPos(HostSlot::List, 0), HostNavKey::Right, 3).pos.field);

	EXPECT_EQ(HostSlot::Field,
		ComputeHostNav(HostFocusPos(HostSlot::List, 0), HostNavKey::Right, kFields, true, true, 3, false).pos.slot);
}

TEST(HostFocus, AnExperienceWithNothingToDecideSendsRightToTheFoot)
{
	// Most entries offer no gameplay controls at all. Right must still land somewhere real.
	EXPECT_EQ(HostSlot::Action, Panel(HostFocusPos(HostSlot::List, 0), HostNavKey::Right, 0).pos.slot);
}

TEST(HostFocus, UpAndDownWalkTheGameplayRows)
{
	EXPECT_EQ(1, Panel(Game(0), HostNavKey::Down, 3).pos.field);
	EXPECT_EQ(2, Panel(Game(1), HostNavKey::Down, 3).pos.field);
	EXPECT_EQ(0, Panel(Game(1), HostNavKey::Up, 3).pos.field);
}

TEST(HostFocus, UpOffTheFirstGameplayRowCrossesBackToTheList)
{
	// The same answer up off the first field gives, because it is the same crossing.
	EXPECT_EQ(HostSlot::List, Panel(Game(0), HostNavKey::Up, 3).pos.slot);
}

TEST(HostFocus, DownOffTheLastGameplayRowLandsOnTheFoot)
{
	// Which is what the panel was leading to: the button that starts the thing being configured.
	EXPECT_EQ(HostSlot::Action, Panel(Game(2), HostNavKey::Down, 3).pos.slot);
}

TEST(HostFocus, UpFromTheFootReturnsToTheLastGameplayRow)
{
	// [rc4l] Both feet answer this, and they answer it the same way. The bug this guards is the two
	// buttons disagreeing about where up goes, which is invisible until somebody presses up on the
	// one that was not tested.
	EXPECT_EQ(HostSlot::Gameplay, Panel(HostFocusPos(HostSlot::Action, 0), HostNavKey::Up, 3).pos.slot);
	EXPECT_EQ(2, Panel(HostFocusPos(HostSlot::Action, 0), HostNavKey::Up, 3).pos.field);

	EXPECT_EQ(HostSlot::Gameplay, Panel(HostFocusPos(HostSlot::Toggle, 0), HostNavKey::Up, 3).pos.slot);
	EXPECT_EQ(2, Panel(HostFocusPos(HostSlot::Toggle, 0), HostNavKey::Up, 3).pos.field);
}

TEST(HostFocus, UpFromTheFootStillSkipsAPanelWithNoRows)
{
	EXPECT_EQ(HostSlot::List, Panel(HostFocusPos(HostSlot::Action, 0), HostNavKey::Up, 0).pos.slot);
}

TEST(HostFocus, LeftAndRightBelongToTheGameplayRowItself)
{
	// A slider moves a stop, an axis of pills moves along its options. Movement and traversal are
	// separate answers: the step is reported and the focus does not move.
	const HostNavResult left = Panel(Game(1), HostNavKey::Left, 3);
	const HostNavResult right = Panel(Game(1), HostNavKey::Right, 3);

	EXPECT_EQ(-1, left.choiceStep);
	EXPECT_EQ(1, right.choiceStep);
	EXPECT_EQ(HostSlot::Gameplay, left.pos.slot);
	EXPECT_EQ(1, left.pos.field);
	EXPECT_EQ(1, right.pos.field);
}

TEST(HostFocus, AGameplayRowThatIsNoLongerThereIsCorrected)
{
	// The panel changes underneath a focus that was legitimate: opening the settings replaces it,
	// and choosing a different experience can leave fewer rows than there were.
	EXPECT_EQ(HostSlot::Action, ClampHostFocus(Game(0), kFields, false, true, 0, false).slot);
	EXPECT_EQ(HostSlot::Action, ClampHostFocus(Game(0), kFields, true, true, 3, false).slot)
		<< "the form replaces the panel, so a gameplay row cannot be current while it is open";
	EXPECT_EQ(0, ClampHostFocus(Game(9), kFields, false, true, 3, false).field);
	EXPECT_EQ(0, ClampHostFocus(Game(-2), kFields, false, true, 3, false).field);
}

TEST(HostFocus, AValidGameplayRowIsLeftAlone)
{
	const HostFocusPos pos = ClampHostFocus(Game(2), kFields, false, true, 3, false);

	EXPECT_EQ(HostSlot::Gameplay, pos.slot);
	EXPECT_EQ(2, pos.field);
}

// ---------------------------------------------------------------- the copy button

namespace
{

HostFocusPos Copy() { return HostFocusPos(HostSlot::Copy, 0); }

// The ordinary form with COPY TO NEW under the visibility row.
HostNavResult CopyNav(HostFocusPos pos, HostNavKey key)
{
	return ComputeHostNav(pos, key, kFields, true, true, 0, true);
}

} // namespace

TEST(HostFocus, DownFromVisibilityReachesTheCopyButton)
{
	// The whole reason it needs a slot: without one it is a button you can see and cannot reach.
	EXPECT_EQ(HostSlot::Copy, CopyNav(Vis(), HostNavKey::Down).pos.slot);
}

TEST(HostFocus, DownFromVisibilityStillReachesTheFootWithoutIt)
{
	// An experience missing a file is the ordinary case, and it must not change.
	EXPECT_EQ(HostSlot::Action, Nav(Vis(), HostNavKey::Down).pos.slot);
}

TEST(HostFocus, TheCopyButtonSitsBetweenTheRowAboveAndTheFoot)
{
	EXPECT_EQ(HostSlot::Visibility, CopyNav(Copy(), HostNavKey::Up).pos.slot);
	EXPECT_EQ(HostSlot::Action, CopyNav(Copy(), HostNavKey::Down).pos.slot);
}

TEST(HostFocus, UpFromTheFootMeetsTheCopyButtonFirst)
{
	// Both foot buttons answer up, and both must agree about what is above them.
	EXPECT_EQ(HostSlot::Copy, CopyNav(Action(), HostNavKey::Up).pos.slot);
	EXPECT_EQ(HostSlot::Copy, CopyNav(Toggle(), HostNavKey::Up).pos.slot);
}

TEST(HostFocus, SidewaysOffTheCopyButtonGoesNowhere)
{
	// One button on its own line. Left is not "the foot", it is nothing.
	EXPECT_EQ(HostSlot::Copy, CopyNav(Copy(), HostNavKey::Left).pos.slot);
	EXPECT_EQ(HostSlot::Copy, CopyNav(Copy(), HostNavKey::Right).pos.slot);

	EXPECT_EQ(0, CopyNav(Copy(), HostNavKey::Left).choiceStep) << "it has no choices to step";
	EXPECT_FALSE(CopyNav(Copy(), HostNavKey::Left).caret);
}

TEST(HostFocus, ACopyButtonThatIsNoLongerOfferedIsCorrected)
{
	// It comes and goes with what is on disk, not only with the panel: switching to an experience
	// whose files are missing takes it away underneath a focus that was on it a frame ago.
	EXPECT_EQ(HostSlot::Action, ClampHostFocus(Copy(), kFields, true, true, 0, false).slot);
	EXPECT_EQ(HostSlot::Copy, ClampHostFocus(Copy(), kFields, true, true, 0, true).slot);
}

TEST(HostFocus, NavigatingFromAStaleCopyPositionStartsFromSomethingReal)
{
	// ComputeHostNav clamps before it answers, so a key pressed on the frame it vanished behaves
	// like one pressed on the foot rather than moving from a place that is not there.
	EXPECT_EQ(HostSlot::Toggle, Nav(Copy(), HostNavKey::Right).pos.slot);
}
