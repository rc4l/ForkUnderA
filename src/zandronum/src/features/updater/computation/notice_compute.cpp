// [rc4l] See notice_compute.h. Pure logic only — unit-tested at 100% line coverage off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/computation/notice_compute.h"

namespace zx { namespace updater {

NoticeStep ComputeNoticeKey(NoticeState s, bool available, NoticeKey key)
{
	// No update pending -> the chip can't be focused; hand everything to the list.
	if (!available)
	{
		s.focused = false;
		return NoticeStep{ s, NoticeAction::Delegate };
	}

	if (s.focused)
	{
		switch (key)
		{
		case NoticeKey::Enter:
			return NoticeStep{ s, NoticeAction::Activate };
		case NoticeKey::Right:
			return NoticeStep{ s, NoticeAction::Handled }; // already the rightmost element
		case NoticeKey::Left:
		case NoticeKey::Back:
			// Leave the chip, restoring the list item we came from so the cursor doesn't jump.
			s.focused = false;
			s.selected = s.prevSelected;
			return NoticeStep{ s, NoticeAction::Handled };
		case NoticeKey::Up:
		case NoticeKey::Down:
			// Step back into the list and let it move from the cleared (-1) selection.
			s.focused = false;
			return NoticeStep{ s, NoticeAction::Delegate };
		default:
			return NoticeStep{ s, NoticeAction::Handled }; // swallow anything else while focused
		}
	}

	if (key == NoticeKey::Right)
	{
		// Take focus: remember where the list was (if anywhere) and clear it, so exactly one of the
		// chip / the list is ever selected.
		if (s.selected >= 0)
			s.prevSelected = s.selected;
		s.focused = true;
		s.selected = -1;
		return NoticeStep{ s, NoticeAction::Handled };
	}
	return NoticeStep{ s, NoticeAction::Delegate };
}

bool ComputeMouseActs(int lastX, int lastY, int x, int y, bool isClickOrRelease)
{
	return isClickOrRelease || x != lastX || y != lastY;
}

} } // namespace zx::updater
