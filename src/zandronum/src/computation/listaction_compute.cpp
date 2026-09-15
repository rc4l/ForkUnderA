// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/listaction_compute.h"

namespace zx
{

ListActionStep StepListAction(ListActionZone zone, ListActionKey key, bool actionAvailable)
{
	ListActionStep out;
	out.zone = zone;

	if (zone == ListActionZone::Action)
	{
		// [rc4l] The return edge first. A zone you can enter and cannot leave is worse than one you
		// cannot enter at all: the pointer user never notices, and the keyboard user is stuck.
		if (key == ListActionKey::Left)
		{
			out.zone = ListActionZone::List;
			return out;
		}

		if (key == ListActionKey::Enter)
		{
			out.activate = true;
			return out;
		}

		// Down goes back into the list AND moves on, because the button sits beside a row and the
		// next thing below it is the next row. Up and anything else is the caller's layout.
		if (key == ListActionKey::Down)
		{
			out.zone = ListActionZone::List;
			out.rowStep = 1;
			return out;
		}

		out.zone = ListActionZone::Outside;
		return out;
	}

	switch (key)
	{
	case ListActionKey::Right:
	case ListActionKey::Enter:
		// [rc4l] Enter on a row does NOT act. It moves to the button, which is where acting happens
		// -- the difference between a list you can read and one that launches under your hands. With
		// no button to move to the focus stays put rather than landing somewhere undrawn.
		if (actionAvailable)
			out.zone = ListActionZone::Action;
		return out;

	case ListActionKey::Up:
		out.rowStep = -1;
		return out;

	case ListActionKey::Down:
		out.rowStep = 1;
		return out;

	default:
		break;
	}

	// Left out of the list: there is nothing that way in either menu, and wrapping round to the
	// button would make the two horizontal keys disagree about which way the layout runs.
	out.zone = ListActionZone::Outside;
	return out;
}

} // namespace zx
