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

} // namespace zx
