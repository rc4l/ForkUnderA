// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "toolgrid_compute.h"

namespace zx
{

GridMove ComputeGridMove(int sel, int count, int cols, GridKey key)
{
	// [rc4l] A grid with no cells or no width has nowhere to go, and answering anything else here
	// would be the caller's divide-by-zero rather than its bug.
	if ((count <= 0) || (cols <= 0))
		return GridMove(sel, false);

	if (sel < 0)
		sel = 0;
	if (sel >= count)
		sel = count - 1;

	const int col = sel % cols;
	const int row = sel / cols;

	switch (key)
	{
	case GridKey::Left:
		return GridMove((col > 0) ? sel - 1 : sel, false);

	case GridKey::Right:
		// Both tests matter: the end of a full row, and the end of a short last one. Either way there
		// is no cell to the right, which is the grid's edge and the caller's cue.
		if ((col + 1 < cols) && (sel + 1 < count))
			return GridMove(sel + 1, false);

		return GridMove(sel, true);

	case GridKey::Up:
		if (row > 0)
			return GridMove(sel - cols, false);

		// Off the top: the grid is done with this key.
		return GridMove(sel, true);

	case GridKey::Down:
		return GridMove((sel + cols < count) ? sel + cols : sel, false);
	}

	return GridMove(sel, false);
}

RightExit ComputeRightExitFromList(bool bLoadOrderHasRows)
{
	return bLoadOrderHasRows ? RightExit::LoadOrder : RightExit::Foot;
}

ListStep ComputeListStep(int sel, int count, int step)
{
	// Nothing to stand on: the key belongs to whatever is that way.
	if (count <= 0)
		return (step < 0) ? ListStep::LeaveUp : ListStep::LeaveDown;

	const int next = sel + step;
	if ((next >= 0) && (next < count))
		return ListStep::Move;

	return (step < 0) ? ListStep::LeaveUp : ListStep::LeaveDown;
}

FootExit ComputeFootExit(GridKey key, int sel, bool bLoadOrderHasRows)
{
	if (key == GridKey::Left)
		return (sel <= 0) ? FootExit::SettingsGrid : FootExit::StayOnRow;

	if (key == GridKey::Up)
		return bLoadOrderHasRows ? FootExit::LoadOrder : FootExit::IwadRow;

	// Right along the row, and down off the bottom of the screen: the caller's own business.
	return FootExit::StayOnRow;
}

ModalPos ComputeModalStep(ModalPos pos, int bodyCount, int footCount, int step)
{
	if (footCount <= 0)
		footCount = 1;			// every box has at least a way out

	const int dir = (step < 0) ? -1 : 1;

	if (pos.region == ModalRegion::Footer)
	{
		// Off the footer is the body, at the end nearest the key: up meets its last row, down its
		// first. An empty body has no row to meet, so the footer keeps the key.
		if (bodyCount <= 0)
			return pos;

		return ModalPos(ModalRegion::Body, (dir < 0) ? (bodyCount - 1) : 0);
	}

	if (bodyCount <= 0)
		return ModalPos(ModalRegion::Footer, 0);

	const int next = pos.index + dir;
	if ((next >= 0) && (next < bodyCount))
		return ModalPos(ModalRegion::Body, next);

	// Off either end of the body is the footer, and DONE is first on every box, so this is always
	// the way out rather than whichever button happened to be under the cursor last.
	return ModalPos(ModalRegion::Footer, 0);
}

} // namespace zx
