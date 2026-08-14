// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-hosting/computation/hostfocus_compute.h"

namespace zx
{

namespace
{

// The foot's left-hand button, which is the only thing on the form that is always there.
HostFocusPos Foot()
{
	return HostFocusPos(HostSlot::Action, 0);
}

// [rc4l] Where UP off the foot lands, written once because both buttons on that row answer it and
// two copies of a three-way choice is two chances to disagree.
HostFocusPos UpFromTheFoot(bool bFields, bool bGameplay, int gameplayRows)
{
	if (bFields)
		return HostFocusPos(HostSlot::Visibility, 0);
	if (bGameplay)
		return HostFocusPos(HostSlot::Gameplay, gameplayRows - 1);

	return HostFocusPos(HostSlot::List, 0);
}

} // namespace

HostFocusPos HostLeftOfTheForm()
{
	return HostFocusPos(HostSlot::List, 0);
}

HostFocusPos ClampHostFocus(HostFocusPos pos, int fieldCount, bool hasFields, bool hasToggle,
                            int gameplayRows)
{
	if ((pos.slot == HostSlot::Field) || (pos.slot == HostSlot::Visibility))
	{
		if (!hasFields)
			return Foot();
	}

	if (pos.slot == HostSlot::Gameplay)
	{
		// The panel is drawn only while the form is shut, and an experience may offer nothing to
		// decide. Both leave a focus here with nothing under it.
		if ((gameplayRows <= 0) || hasFields)
			return Foot();

		if ((pos.field < 0) || (pos.field >= gameplayRows))
			return HostFocusPos(HostSlot::Gameplay, 0);
	}

	if (pos.slot == HostSlot::Field)
	{
		// A field index past the end is the same kind of stale as a slot that no longer exists.
		if ((pos.field < 0) || (pos.field >= fieldCount))
			return (fieldCount > 0) ? HostFocusPos(HostSlot::Field, 0) : Foot();
	}

	if ((pos.slot == HostSlot::Toggle) && !hasToggle)
		return Foot();

	return pos;
}

HostNavResult ComputeHostNav(HostFocusPos pos, HostNavKey key, int fieldCount,
                             bool hasFields, bool hasToggle, int gameplayRows)
{
	HostNavResult out;

	// Start from something that exists. The settings can be shut, or a server started, underneath a
	// focus that was legitimate when it was set.
	pos = ClampHostFocus(pos, fieldCount, hasFields, hasToggle, gameplayRows);
	out.pos = pos;

	const bool bFields = hasFields && (fieldCount > 0);

	// [rc4l] The gameplay panel and the form are never both up: the form REPLACES the panel. So the
	// right column has one first thing, and which one it is depends on which face is showing.
	const bool bGameplay = !hasFields && (gameplayRows > 0);

	switch (pos.slot)
	{
	case HostSlot::List:
		// [rc4l] Up and down move the SELECTION rather than focus, the same split the browser's rows
		// make. Up off the first row is the caller's to notice: it reports the step and the caller,
		// which knows whether row 0 is already current, hands focus back to the tabs.
		if (key == HostNavKey::Up)
		{
			out.rowStep = -1;
			return out;
		}
		if (key == HostNavKey::Down)
		{
			out.rowStep = 1;
			return out;
		}

		// Right crosses to the right column: the first field when the settings are open, the first
		// gameplay row when they are shut and there is one, the foot when there is neither. Left has
		// nowhere to go; the list is the leftmost thing here.
		if (key == HostNavKey::Right)
		{
			if (bFields)
				out.pos = HostFocusPos(HostSlot::Field, 0);
			else if (bGameplay)
				out.pos = HostFocusPos(HostSlot::Gameplay, 0);
			else
				out.pos = Foot();
		}
		return out;

	case HostSlot::Field:
		// [rc4l] Left and right are the CARET's, not navigation. A text box that jumped to another
		// control when you tried to move through what you had typed would be unusable, which is the
		// same reason the browser's search box refuses them.
		if ((key == HostNavKey::Left) || (key == HostNavKey::Right))
		{
			out.caret = true;
			return out;
		}

		if (key == HostNavKey::Up)
		{
			if (pos.field > 0)
				out.pos = HostFocusPos(HostSlot::Field, pos.field - 1);
			else
				out.pos = HostFocusPos(HostSlot::List, 0);	// back across to the list
			return out;
		}

		if (pos.field + 1 < fieldCount)
			out.pos = HostFocusPos(HostSlot::Field, pos.field + 1);
		else
			out.pos = HostFocusPos(HostSlot::Visibility, 0);
		return out;

	case HostSlot::Gameplay:
		// [rc4l] Left and right belong to the ROW, not to navigation: a slider moves a stop, an axis
		// of pills moves along its options. Reported as a step and applied by the caller, which is
		// the only side that knows what the focused row actually is.
		if (key == HostNavKey::Left)
		{
			out.choiceStep = -1;
			return out;
		}
		if (key == HostNavKey::Right)
		{
			out.choiceStep = 1;
			return out;
		}

		if (key == HostNavKey::Up)
		{
			// Up off the first row crosses back to the list, the way up off the first field does.
			out.pos = (pos.field > 0)
				? HostFocusPos(HostSlot::Gameplay, pos.field - 1)
				: HostFocusPos(HostSlot::List, 0);
			return out;
		}

		// Down off the last row lands on the foot, which is what the panel was leading to.
		out.pos = (pos.field + 1 < gameplayRows)
			? HostFocusPos(HostSlot::Gameplay, pos.field + 1)
			: Foot();
		return out;

	case HostSlot::Visibility:
		// Left and right pick the choice. Movement and traversal are separate answers, so this
		// reports the step and leaves focus alone.
		if (key == HostNavKey::Left)
		{
			out.choiceStep = -1;
			return out;
		}
		if (key == HostNavKey::Right)
		{
			out.choiceStep = 1;
			return out;
		}

		if (key == HostNavKey::Up)
		{
			out.pos = (fieldCount > 0)
				? HostFocusPos(HostSlot::Field, fieldCount - 1)
				: HostFocusPos(HostSlot::List, 0);
			return out;
		}

		out.pos = Foot();
		return out;

	case HostSlot::Action:
		// [rc4l] The foot is a ROW. Right reaches the toggle beside it when there is one; left has
		// nowhere to go, because the action is the leftmost thing on it.
		if (key == HostNavKey::Right)
		{
			if (hasToggle)
				out.pos = HostFocusPos(HostSlot::Toggle, 0);
			return out;
		}
		if (key == HostNavKey::Left)
		{
			out.pos = HostFocusPos(HostSlot::List, 0);
			return out;
		}

		if (key == HostNavKey::Up)
		{
			out.pos = UpFromTheFoot(bFields, bGameplay, gameplayRows);
			return out;
		}

		// Down from the foot: nothing is below it, so nothing moves. Deliberately not a wrap --
		// jumping back to the top of a form because you pressed down once more is a surprise, and
		// the button is where the form was trying to get you anyway.
		return out;

	case HostSlot::Toggle:
		if (key == HostNavKey::Left)
		{
			out.pos = Foot();
			return out;
		}
		if (key == HostNavKey::Right)
			return out;

		if (key == HostNavKey::Up)
		{
			out.pos = UpFromTheFoot(bFields, bGameplay, gameplayRows);
			return out;
		}

		return out;

	case HostSlot::Away:
		break;
	}

	// Away: coming back in lands on the list, which is what the panel is for.
	if (key == HostNavKey::Down)
		out.pos = HostFocusPos(HostSlot::List, 0);

	return out;
}

} // namespace zx
