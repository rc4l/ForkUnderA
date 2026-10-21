// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/touch-input/computation/touchdef_compute.h"

#include <gtest/gtest.h>

using namespace zx::touch;

namespace {

TouchButtonDef Button(const char *id, int key, float ax, float ay, float size)
{
	TouchButtonDef b;
	b.id = id; b.key = key; b.ax = ax; b.ay = ay; b.size = size; b.label = id;
	return b;
}

// [rc4l] A 960x540 view with no insets: twice the 480x270 canvas, so a virtual pixel is two view pixels.
const TouchViewIn kView = { 960, 540, 0.f, 0.f, 0.f, 0.f, false };

TouchLayout TwoButtons()
{
	TouchLayout layout = DefaultTouchLayout();
	MergeTouchButton(layout, Button("fire", 0x1BF, 0.9f, 0.8f, 80.f));
	MergeTouchButton(layout, Button("use", 0x1C0, 0.9f, 0.6f, 60.f));
	return layout;
}

TouchPointerIn At(int id, float x, float y, bool ended = false)
{
	const TouchPointerIn p = { id, x, y, x, y, ended };
	return p;
}

// [rc4l] A pointer already moved from where it landed, as a flick before the first tic arrives.
TouchPointerIn Flicked(int id, float ox, float oy, float x, float y)
{
	const TouchPointerIn p = { id, x, y, ox, oy, false };
	return p;
}

} // namespace

TEST(TouchDef, ALaterButtonReplacesTheOneWithItsIdAndANewOneAppends)
{
	TouchLayout layout = TwoButtons();
	ASSERT_EQ(layout.buttons.size(), 2u);
	MergeTouchButton(layout, Button("fire", 0x108, 0.5f, 0.5f, 40.f));
	ASSERT_EQ(layout.buttons.size(), 2u);
	EXPECT_EQ(layout.buttons[0].key, 0x108);
	EXPECT_FLOAT_EQ(layout.buttons[0].size, 40.f);
	MergeTouchButton(layout, Button("jump", 0x1C3, 0.8f, 0.9f, 60.f));
	EXPECT_EQ(layout.buttons.size(), 3u);
	ClearTouchLayout(layout);
	EXPECT_TRUE(layout.buttons.empty());
	EXPECT_FLOAT_EQ(layout.stick.radius, 64.f);
}

TEST(TouchDef, AnOverrideSetsWhatItNamesAndKeepsTheRest)
{
	std::vector<TouchButtonOverride> portrait;
	const TouchButtonOverride at = { "fire", true, 0.8f, 0.9f, false, 0.f };
	const TouchButtonOverride size = { "fire", false, 0.f, 0.f, true, 100.f };
	MergeTouchOverride(portrait, at);
	MergeTouchOverride(portrait, size);
	ASSERT_EQ(portrait.size(), 1u);
	EXPECT_TRUE(portrait[0].hasAt);
	EXPECT_FLOAT_EQ(portrait[0].ax, 0.8f);
	EXPECT_TRUE(portrait[0].hasSize);
	EXPECT_FLOAT_EQ(portrait[0].size, 100.f);
	const TouchButtonOverride other = { "use", true, 0.1f, 0.1f, false, 0.f };
	MergeTouchOverride(portrait, other);
	EXPECT_EQ(portrait.size(), 2u);
}

TEST(TouchDef, ControlsAreLaidOnTheViewInsideItsInsets)
{
	const TouchRects r = ComputeTouchRects(TwoButtons(), kView, 1.f, false);
	EXPECT_FLOAT_EQ(r.scale, 2.f);
	EXPECT_FLOAT_EQ(r.stickZone.x, 0.f);
	EXPECT_FLOAT_EQ(r.stickZone.w, 384.f);
	EXPECT_FLOAT_EQ(r.stickZone.y, 162.f);
	EXPECT_FLOAT_EQ(r.stickRadius, 128.f);
	ASSERT_EQ(r.buttons.size(), 2u);
	EXPECT_EQ(r.buttons[0].index, 0);
	EXPECT_FLOAT_EQ(r.buttons[0].rect.x, 864.f - 80.f);
	EXPECT_FLOAT_EQ(r.buttons[0].rect.y, 432.f - 80.f);
	EXPECT_FLOAT_EQ(r.buttons[0].rect.w, 160.f);

	// [rc4l] A narrow view is scaled by its width, the smaller fit, and insets shrink the box the anchors lie in.
	const TouchViewIn tall = { 480, 960, 10.f, 20.f, 10.f, 40.f, true };
	const TouchRects t = ComputeTouchRects(TwoButtons(), tall, 1.f, false);
	EXPECT_FLOAT_EQ(t.scale, 1.f);
	EXPECT_FLOAT_EQ(t.buttons[0].rect.w, 80.f);
	EXPECT_FLOAT_EQ(t.buttons[0].rect.x, 10.f + 0.9f * 460.f - 40.f);
	EXPECT_FLOAT_EQ(t.buttons[0].rect.y, 20.f + 0.8f * 900.f - 40.f);

	// [rc4l] A view with no height still lays out, at the plain scale.
	const TouchViewIn none = { 0, 0, 0.f, 0.f, 0.f, 0.f, false };
	EXPECT_FLOAT_EQ(ComputeTouchRects(TwoButtons(), none, 0.f, false).scale, 1.f);
}

TEST(TouchDef, OrientationOverridesAndTheSizeSettingAndLeftHandednessApply)
{
	TouchLayout layout = TwoButtons();
	const TouchButtonOverride p = { "fire", true, 0.5f, 0.5f, true, 100.f };
	MergeTouchOverride(layout.portrait, p);
	const TouchButtonOverride l = { "fire", false, 0.f, 0.f, true, 20.f };
	MergeTouchOverride(layout.landscape, l);

	const TouchRects landscape = ComputeTouchRects(layout, kView, 1.5f, false);
	EXPECT_FLOAT_EQ(landscape.scale, 3.f);
	EXPECT_FLOAT_EQ(landscape.buttons[0].rect.w, 60.f);
	EXPECT_FLOAT_EQ(landscape.buttons[0].rect.x, 864.f - 30.f);   // [rc4l] the landscape block set the size alone

	TouchViewIn portraitView = kView;
	portraitView.portrait = true;
	const TouchRects portrait = ComputeTouchRects(layout, portraitView, 1.f, false);
	EXPECT_FLOAT_EQ(portrait.buttons[0].rect.w, 200.f);
	EXPECT_FLOAT_EQ(portrait.buttons[0].rect.x, 480.f - 100.f);

	const TouchRects left = ComputeTouchRects(layout, kView, 1.f, true);
	EXPECT_NEAR(left.buttons[1].rect.x, 96.f - 60.f, 1e-3f);   // [rc4l] 0.9 mirrored to 0.1
	EXPECT_FLOAT_EQ(left.stickZone.x, 576.f);              // [rc4l] the stick's zone on the right
	EXPECT_FLOAT_EQ(left.lookZone.x, 0.f);
}

TEST(TouchDef, TheRectTestIsHalfOpen)
{
	const TouchRect r = { 10.f, 10.f, 20.f, 20.f };
	EXPECT_TRUE(TouchRectHas(r, 10.f, 10.f));
	EXPECT_TRUE(TouchRectHas(r, 29.9f, 29.9f));
	EXPECT_FALSE(TouchRectHas(r, 30.f, 15.f));
	EXPECT_FALSE(TouchRectHas(r, 15.f, 30.f));
	EXPECT_FALSE(TouchRectHas(r, 9.f, 15.f));
	EXPECT_FALSE(TouchRectHas(r, 15.f, 9.f));
}

TEST(TouchFrame, AThumbInTheStickZoneDrivesTheStickFromWhereItLanded)
{
	const TouchRects rects = ComputeTouchRects(TwoButtons(), kView, 1.f, false);
	TouchFrameState state = EmptyTouchFrameState();
	const TouchPointerIn down[] = { At(1, 100.f, 300.f) };
	TouchFrameOut out = ComputeTouchFrame(state, down, 1, rects);
	EXPECT_TRUE(out.stickHeld);
	EXPECT_FLOAT_EQ(out.stickDx, 0.f);
	state = out.next;
	const TouchPointerIn moved[] = { At(1, 130.f, 280.f) };
	out = ComputeTouchFrame(state, moved, 1, rects);
	EXPECT_TRUE(out.stickHeld);
	EXPECT_FLOAT_EQ(out.stickDx, 30.f);
	EXPECT_FLOAT_EQ(out.stickDy, -20.f);
	EXPECT_EQ(out.buttons, 0u);
	state = out.next;
	// [rc4l] A second thumb in the zone does not take the stick: it is a look drag if in that zone, else a button finger.
	const TouchPointerIn two[] = { At(1, 130.f, 280.f), At(2, 200.f, 300.f) };
	out = ComputeTouchFrame(state, two, 2, rects);
	EXPECT_EQ(out.next.slots[1].role, kTouchButton);
	state = out.next;
	const TouchPointerIn lifted[] = { At(1, 130.f, 280.f, true), At(2, 200.f, 300.f) };
	out = ComputeTouchFrame(state, lifted, 2, rects);
	EXPECT_FALSE(out.stickHeld);
	EXPECT_FLOAT_EQ(out.stickDx, 0.f);
	EXPECT_EQ(out.next.slots[0].role, kTouchNone);
	// [rc4l] Gone without an ended report (the host dropped it): released too.
	state = out.next;
	out = ComputeTouchFrame(state, NULL, 0, rects);
	EXPECT_EQ(out.next.slots[1].role, kTouchNone);
}

// [rc4l] The check's first move landed before the engine's next tic, and the origin taken where the pointer was first seen read no deflection at all.
TEST(TouchFrame, AFlickBeforeTheFirstTicKeepsWhereItLanded)
{
	const TouchRects rects = ComputeTouchRects(TwoButtons(), kView, 1.f, false);
	const TouchPointerIn flick[] = { Flicked(1, 100.f, 300.f, 130.f, 280.f) };
	const TouchFrameOut out = ComputeTouchFrame(EmptyTouchFrameState(), flick, 1, rects);
	EXPECT_TRUE(out.stickHeld);
	EXPECT_FLOAT_EQ(out.stickDx, 30.f);
	EXPECT_FLOAT_EQ(out.stickDy, -20.f);
	// [rc4l] And the role is the landing's: a finger that landed on fire and slid into the stick zone is a button finger.
	const TouchPointerIn slid[] = { Flicked(2, 864.f, 432.f, 100.f, 300.f) };
	const TouchFrameOut o2 = ComputeTouchFrame(EmptyTouchFrameState(), slid, 1, rects);
	EXPECT_EQ(o2.next.slots[0].role, kTouchButton);
	EXPECT_FALSE(o2.stickHeld);
}

TEST(TouchFrame, AFingerInTheLookZoneDragsTheViewByItsMovement)
{
	const TouchRects rects = ComputeTouchRects(TwoButtons(), kView, 1.f, false);
	TouchFrameState state = EmptyTouchFrameState();
	const TouchPointerIn down[] = { At(7, 600.f, 200.f) };
	TouchFrameOut out = ComputeTouchFrame(state, down, 1, rects);
	EXPECT_EQ(out.next.slots[0].role, kTouchLook);
	EXPECT_FLOAT_EQ(out.lookDx, 0.f);
	state = out.next;
	const TouchPointerIn moved[] = { At(7, 615.f, 190.f) };
	out = ComputeTouchFrame(state, moved, 1, rects);
	EXPECT_FLOAT_EQ(out.lookDx, 15.f);
	EXPECT_FLOAT_EQ(out.lookDy, -10.f);
	state = out.next;
	out = ComputeTouchFrame(state, moved, 1, rects);
	EXPECT_FLOAT_EQ(out.lookDx, 0.f);   // [rc4l] no movement, no drag
	// [rc4l] A second finger in the look zone becomes a button finger, not a second drag.
	state = out.next;
	const TouchPointerIn two[] = { At(7, 615.f, 190.f), At(8, 700.f, 100.f) };
	out = ComputeTouchFrame(state, two, 2, rects);
	EXPECT_EQ(out.next.slots[1].role, kTouchButton);
}

TEST(TouchFrame, ButtonsAreHeldWhileAFingerIsOnThemAndATapCountsForOneTic)
{
	const TouchRects rects = ComputeTouchRects(TwoButtons(), kView, 1.f, false);
	TouchFrameState state = EmptyTouchFrameState();
	// [rc4l] fire is centred at 864,432; use at 864,324.
	const TouchPointerIn onFire[] = { At(3, 864.f, 432.f) };
	TouchFrameOut out = ComputeTouchFrame(state, onFire, 1, rects);
	EXPECT_EQ(out.buttons, 1u);
	EXPECT_EQ(out.next.slots[0].role, kTouchButton);
	state = out.next;
	const TouchPointerIn slid[] = { At(3, 864.f, 324.f) };
	out = ComputeTouchFrame(state, slid, 1, rects);
	EXPECT_EQ(out.buttons, 2u);   // [rc4l] slid onto use
	state = out.next;
	const TouchPointerIn intoStick[] = { At(3, 100.f, 300.f) };
	out = ComputeTouchFrame(state, intoStick, 1, rects);
	EXPECT_EQ(out.buttons, 0u);
	EXPECT_FALSE(out.stickHeld);   // [rc4l] a button finger never becomes the stick
	state = out.next;
	const TouchPointerIn tap[] = { At(4, 864.f, 432.f, true) };
	out = ComputeTouchFrame(state, tap, 1, rects);
	EXPECT_EQ(out.buttons, 1u);
	EXPECT_EQ(out.next.slots[1].role, kTouchNone);
}

TEST(TouchFrame, MoreThanEightPointersAreIgnoredAndTwoCanHoldTwoButtons)
{
	const TouchRects rects = ComputeTouchRects(TwoButtons(), kView, 1.f, false);
	TouchFrameState state = EmptyTouchFrameState();
	TouchPointerIn many[10];
	for (int i = 0; i < 10; i++) many[i] = At(100 + i, 864.f, i < 5 ? 432.f : 324.f);
	const TouchFrameOut out = ComputeTouchFrame(state, many, 10, rects);
	EXPECT_EQ(out.buttons, 3u);
	int taken = 0;
	for (int i = 0; i < kTouchSlots; i++) if (out.next.slots[i].role != kTouchNone) taken++;
	EXPECT_EQ(taken, kTouchSlots);
}

TEST(TouchShown, TheOverlayShowsInPlayAloneAndOnlyWhenWanted)
{
	TouchShownIn in = { true, false, false, false, false, false, kTouchOn, false, false };
	EXPECT_TRUE(ComputeTouchShown(in));
	TouchShownIn off = in; off.mode = kTouchOff;
	EXPECT_FALSE(ComputeTouchShown(off));
	TouchShownIn menu = in; menu.menuActive = true;
	EXPECT_FALSE(ComputeTouchShown(menu));
	TouchShownIn console = in; console.consoleOpen = true;
	EXPECT_FALSE(ComputeTouchShown(console));
	TouchShownIn automap = in; automap.automapActive = true;
	EXPECT_FALSE(ComputeTouchShown(automap));
	TouchShownIn paused = in; paused.paused = true;
	EXPECT_FALSE(ComputeTouchShown(paused));
	TouchShownIn demo = in; demo.demoPlayback = true;
	EXPECT_FALSE(ComputeTouchShown(demo));
	TouchShownIn title = in; title.inLevel = false;
	EXPECT_FALSE(ComputeTouchShown(title));
	TouchShownIn autoUnseen = in; autoUnseen.mode = kTouchAuto;
	EXPECT_FALSE(ComputeTouchShown(autoUnseen));
	TouchShownIn autoSeen = autoUnseen; autoSeen.touchSeen = true;
	EXPECT_TRUE(ComputeTouchShown(autoSeen));
	TouchShownIn autoKeyboard = autoSeen; autoKeyboard.keyboardSinceTouch = true;
	EXPECT_FALSE(ComputeTouchShown(autoKeyboard));
}
