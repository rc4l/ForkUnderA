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

} // namespace

HostFocusPos HostLeftOfTheForm()
{
	return HostFocusPos(HostSlot::List, 0);
}

HostFocusPos ClampHostFocus(HostFocusPos pos, int fieldCount, bool hasFields, bool hasToggle)
{
	if ((pos.slot == HostSlot::Field) || (pos.slot == HostSlot::Visibility))
	{
		if (!hasFields)
			return Foot();
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
                             bool hasFields, bool hasToggle)
{
	HostNavResult out;

	// Start from something that exists. The settings can be shut, or a server started, underneath a
	// focus that was legitimate when it was set.
	pos = ClampHostFocus(pos, fieldCount, hasFields, hasToggle);
	out.pos = pos;

	const bool bFields = hasFields && (fieldCount > 0);

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

		// Right crosses to the right column: the first field when the settings are open, the foot
		// when they are not. Left has nowhere to go; the list is the leftmost thing here.
		if (key == HostNavKey::Right)
			out.pos = bFields ? HostFocusPos(HostSlot::Field, 0) : Foot();
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
			out.pos = bFields
				? HostFocusPos(HostSlot::Visibility, 0)
				: HostFocusPos(HostSlot::List, 0);
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
			out.pos = bFields
				? HostFocusPos(HostSlot::Visibility, 0)
				: HostFocusPos(HostSlot::List, 0);
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
