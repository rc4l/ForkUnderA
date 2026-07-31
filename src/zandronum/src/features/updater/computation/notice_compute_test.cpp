// [rc4l] Tests for the update-notice focus state machine. Every line/branch (the coverage gate
// enforces 100% on *_compute.cpp). Pins the interaction rules that were fiddly to get right: taking
// focus clears the list, leaving restores the exact item, up/down step back into the list, and a
// resting mouse pointer never acts.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/computation/notice_compute.h"

#include <gtest/gtest.h>

using namespace zx::updater;

namespace {
NoticeState St(bool focused, int selected, int prev) { return NoticeState{ focused, selected, prev }; }
}

// ---- unavailable: chip can never focus -----------------------------------

TEST(NoticeKey, UnavailableAlwaysDelegatesAndClearsFocus)
{
	NoticeStep r = ComputeNoticeKey(St(true, -1, 2), /*available=*/false, NoticeKey::Right);
	EXPECT_FALSE(r.state.focused);
	EXPECT_EQ(r.action, NoticeAction::Delegate);
}

// ---- taking focus --------------------------------------------------------

TEST(NoticeKey, RightFromListTakesFocusAndRemembersItem)
{
	NoticeStep r = ComputeNoticeKey(St(false, 1, 0), true, NoticeKey::Right);
	EXPECT_TRUE(r.state.focused);
	EXPECT_EQ(r.state.selected, -1);      // list cleared
	EXPECT_EQ(r.state.prevSelected, 1);   // remembered where we were
	EXPECT_EQ(r.action, NoticeAction::Handled);
}

TEST(NoticeKey, RightWithNoListSelectionKeepsPrev)
{
	// selected == -1 -> nothing to remember; prev is left untouched.
	NoticeStep r = ComputeNoticeKey(St(false, -1, 3), true, NoticeKey::Right);
	EXPECT_TRUE(r.state.focused);
	EXPECT_EQ(r.state.prevSelected, 3);
}

TEST(NoticeKey, NonRightWhileUnfocusedDelegates)
{
	for (NoticeKey k : { NoticeKey::Left, NoticeKey::Up, NoticeKey::Down, NoticeKey::Enter,
			NoticeKey::Back, NoticeKey::Other })
	{
		NoticeStep r = ComputeNoticeKey(St(false, 2, 0), true, k);
		EXPECT_FALSE(r.state.focused);
		EXPECT_EQ(r.action, NoticeAction::Delegate) << "key index " << (int)k;
	}
}

// ---- while focused -------------------------------------------------------

TEST(NoticeKey, EnterActivates)
{
	NoticeStep r = ComputeNoticeKey(St(true, -1, 2), true, NoticeKey::Enter);
	EXPECT_EQ(r.action, NoticeAction::Activate);
	EXPECT_TRUE(r.state.focused); // still focused; the engine opens the dialog on top
}

TEST(NoticeKey, RightWhileFocusedIsNoop)
{
	NoticeStep r = ComputeNoticeKey(St(true, -1, 2), true, NoticeKey::Right);
	EXPECT_TRUE(r.state.focused);
	EXPECT_EQ(r.action, NoticeAction::Handled);
}

TEST(NoticeKey, LeftAndBackRestorePreviousItem)
{
	for (NoticeKey k : { NoticeKey::Left, NoticeKey::Back })
	{
		NoticeStep r = ComputeNoticeKey(St(true, -1, 2), true, k);
		EXPECT_FALSE(r.state.focused);
		EXPECT_EQ(r.state.selected, 2);   // restored to where we came from
		EXPECT_EQ(r.action, NoticeAction::Handled);
	}
}

TEST(NoticeKey, UpDownStepBackIntoList)
{
	for (NoticeKey k : { NoticeKey::Up, NoticeKey::Down })
	{
		NoticeStep r = ComputeNoticeKey(St(true, -1, 2), true, k);
		EXPECT_FALSE(r.state.focused);
		EXPECT_EQ(r.state.selected, -1);  // left cleared; base moves from -1
		EXPECT_EQ(r.action, NoticeAction::Delegate);
	}
}

TEST(NoticeKey, OtherKeyWhileFocusedIsSwallowed)
{
	NoticeStep r = ComputeNoticeKey(St(true, -1, 2), true, NoticeKey::Other);
	EXPECT_TRUE(r.state.focused);
	EXPECT_EQ(r.action, NoticeAction::Handled);
}

// ---- mouse moved-gate ----------------------------------------------------

TEST(MouseActs, MovementOrClickActsRestDoesNot)
{
	EXPECT_FALSE(ComputeMouseActs(100, 100, 100, 100, false)); // parked pointer, move event -> ignore
	EXPECT_TRUE(ComputeMouseActs(100, 100, 101, 100, false));  // moved in x
	EXPECT_TRUE(ComputeMouseActs(100, 100, 100, 101, false));  // moved in y
	EXPECT_TRUE(ComputeMouseActs(100, 100, 100, 100, true));   // click/release always acts
}
