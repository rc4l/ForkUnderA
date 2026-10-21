// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/touch-input/computation/touchdef_compute.h"

namespace zx { namespace touch {

namespace {

// [rc4l] The freeform menu's virtual canvas (ScaleFac in v_video.cpp): the smaller of the view's width over 480 and height over 270, so a size reads the same there and here and the HUD font scaled the same way fits the pills.
const float kVirtualWidth = 480.f, kVirtualHeight = 270.f;

const TouchStickDef kDefaultStick = { { 0.f, 0.3f, 0.4f, 1.f }, 64.f, 0.2f };
const TouchLookDef kDefaultLook = { { 0.4f, 0.f, 1.f, 1.f }, 1.f };

int IndexOfButton(const TouchLayout &layout, const std::string &id)
{
	for (size_t i = 0; i < layout.buttons.size(); i++)
		if (layout.buttons[i].id == id) return (int)i;
	return -1;
}

const TouchButtonOverride *OverrideFor(const std::vector<TouchButtonOverride> &overrides, const std::string &id)
{
	for (size_t i = 0; i < overrides.size(); i++)
		if (overrides[i].id == id) return &overrides[i];
	return NULL;
}

TouchRect ZoneRect(const TouchZone &zone, const TouchRect &box, bool mirror)
{
	const float x0 = mirror ? 1.f - zone.x1 : zone.x0;
	const float x1 = mirror ? 1.f - zone.x0 : zone.x1;
	const TouchRect r = { box.x + x0 * box.w, box.y + zone.y0 * box.h, (x1 - x0) * box.w, (zone.y1 - zone.y0) * box.h };
	return r;
}

int SlotOf(const TouchFrameState &state, int id)
{
	for (int i = 0; i < kTouchSlots; i++)
		if (state.slots[i].role != kTouchNone && state.slots[i].id == id) return i;
	return -1;
}

int FreeSlot(const TouchFrameState &state)
{
	for (int i = 0; i < kTouchSlots; i++)
		if (state.slots[i].role == kTouchNone) return i;
	return -1;
}

unsigned ButtonsUnder(const TouchRects &rects, float x, float y)
{
	unsigned mask = 0;
	for (size_t i = 0; i < rects.buttons.size(); i++)
		if (TouchRectHas(rects.buttons[i].rect, x, y)) mask |= 1u << rects.buttons[i].index;
	return mask;
}

} // namespace

TouchLayout DefaultTouchLayout()
{
	TouchLayout layout;
	layout.stick = kDefaultStick;
	layout.look = kDefaultLook;
	return layout;
}

void ClearTouchLayout(TouchLayout &layout)
{
	layout = DefaultTouchLayout();
}

void MergeTouchButton(TouchLayout &layout, const TouchButtonDef &button)
{
	const int at = IndexOfButton(layout, button.id);
	if (at >= 0) layout.buttons[at] = button;
	else layout.buttons.push_back(button);
}

void MergeTouchOverride(std::vector<TouchButtonOverride> &overrides, const TouchButtonOverride &override)
{
	for (size_t i = 0; i < overrides.size(); i++)
	{
		if (overrides[i].id != override.id) continue;
		// [rc4l] A later line sets what it names and keeps the rest of the earlier one.
		if (override.hasAt) { overrides[i].hasAt = true; overrides[i].ax = override.ax; overrides[i].ay = override.ay; }
		if (override.hasSize) { overrides[i].hasSize = true; overrides[i].size = override.size; }
		return;
	}
	overrides.push_back(override);
}

bool TouchRectHas(const TouchRect &rect, float x, float y)
{
	return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

TouchRects ComputeTouchRects(const TouchLayout &layout, const TouchViewIn &view, float sizeScale, bool leftHanded)
{
	TouchRects out;
	const TouchRect box = { view.insetLeft, view.insetTop, (float)view.width - view.insetLeft - view.insetRight, (float)view.height - view.insetTop - view.insetBottom };
	const float fit = view.width > 0 && view.height > 0 ? ((float)view.width / kVirtualWidth < (float)view.height / kVirtualHeight ? (float)view.width / kVirtualWidth : (float)view.height / kVirtualHeight) : 1.f;
	out.scale = fit * (sizeScale > 0.f ? sizeScale : 1.f);
	out.stickZone = ZoneRect(layout.stick.zone, box, leftHanded);
	out.lookZone = ZoneRect(layout.look.zone, box, leftHanded);
	out.stickRadius = layout.stick.radius * out.scale;
	const std::vector<TouchButtonOverride> &overrides = view.portrait ? layout.portrait : layout.landscape;
	for (size_t i = 0; i < layout.buttons.size(); i++)
	{
		const TouchButtonDef &b = layout.buttons[i];
		float ax = b.ax, ay = b.ay, size = b.size;
		if (const TouchButtonOverride *o = OverrideFor(overrides, b.id))
		{
			if (o->hasAt) { ax = o->ax; ay = o->ay; }
			if (o->hasSize) size = o->size;
		}
		if (leftHanded) ax = 1.f - ax;
		const float px = size * out.scale;
		const TouchButtonRect r = { (int)i, { box.x + ax * box.w - px * 0.5f, box.y + ay * box.h - px * 0.5f, px, px } };
		out.buttons.push_back(r);
	}
	return out;
}

TouchFrameState EmptyTouchFrameState()
{
	TouchFrameState state;
	for (int i = 0; i < kTouchSlots; i++) { state.slots[i].id = 0; state.slots[i].role = kTouchNone; state.slots[i].ox = 0.f; state.slots[i].oy = 0.f; }
	return state;
}

TouchFrameOut ComputeTouchFrame(const TouchFrameState &prev, const TouchPointerIn *pointers, int count, const TouchRects &rects)
{
	TouchFrameOut out;
	out.stickHeld = false; out.stickDx = 0.f; out.stickDy = 0.f; out.lookDx = 0.f; out.lookDy = 0.f; out.buttons = 0;
	out.next = prev;

	bool present[kTouchSlots] = { false, false, false, false, false, false, false, false };
	for (int p = 0; p < count; p++)
	{
		const TouchPointerIn &in = pointers[p];
		int slot = SlotOf(out.next, in.id);
		if (slot < 0)
		{
			// [rc4l] A new pointer takes the first role its place offers: a button under it, else the stick's zone, else the look's.
			slot = FreeSlot(out.next);
			if (slot < 0) continue;
			TouchSlot &s = out.next.slots[slot];
			s.id = in.id; s.ox = in.ox; s.oy = in.oy;
			bool stickTaken = false, lookTaken = false;
			for (int i = 0; i < kTouchSlots; i++)
			{
				if (out.next.slots[i].role == kTouchStick) stickTaken = true;
				if (out.next.slots[i].role == kTouchLook) lookTaken = true;
			}
			if (ButtonsUnder(rects, in.ox, in.oy) != 0) s.role = kTouchButton;
			else if (!stickTaken && TouchRectHas(rects.stickZone, in.ox, in.oy)) s.role = kTouchStick;
			else if (!lookTaken && TouchRectHas(rects.lookZone, in.ox, in.oy)) s.role = kTouchLook;
			else s.role = kTouchButton;   // [rc4l] Nowhere in particular: it may still slide onto a button.
		}
		present[slot] = true;
		TouchSlot &s = out.next.slots[slot];
		switch (s.role)
		{
		case kTouchStick:
			out.stickHeld = true;
			out.stickDx = in.x - s.ox;
			out.stickDy = in.y - s.oy;
			break;
		case kTouchLook:
			out.lookDx += in.x - s.ox;
			out.lookDy += in.y - s.oy;
			s.ox = in.x; s.oy = in.y;
			break;
		default:
			// [rc4l] Held this tic even when it lifted, so a tap between two tics presses for one.
			out.buttons |= ButtonsUnder(rects, in.x, in.y);
			break;
		}
		if (in.ended) present[slot] = false;
	}
	for (int i = 0; i < kTouchSlots; i++)
	{
		if (!present[i] && out.next.slots[i].role != kTouchNone)
		{
			if (out.next.slots[i].role == kTouchStick) { out.stickHeld = false; out.stickDx = 0.f; out.stickDy = 0.f; }
			out.next.slots[i].role = kTouchNone;
		}
	}
	return out;
}

bool ComputeTouchShown(const TouchShownIn &in)
{
	if (in.mode == kTouchOff) return false;
	if (!in.inLevel || in.menuActive || in.consoleOpen || in.automapActive || in.paused || in.demoPlayback) return false;
	if (in.mode == kTouchOn) return true;
	return in.touchSeen && !in.keyboardSinceTouch;
}

}} // namespace zx::touch
