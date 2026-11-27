// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "lookorder_compute.h"

void ComputeLookOrderStart(ZxLookOrder &order, const bool *inGame, int slots)
{
	order.count = 0;
	const int last = slots < kZxLookSlots ? slots : kZxLookSlots;
	for (int slot = 0; slot < last; ++slot)
	{
		if (inGame[slot])
			order.left[order.count++] = slot;
	}
}

int ComputeLookOrderNext(ZxLookOrder &order, ZxLookDraw draw)
{
	if (order.count == 0)
		return -1;
	// [Doruk-Aksoy] The picked player is replaced by the last untried one, a Fisher-Yates step taken one player at a time.
	const int pick = order.count > 1 ? draw(order.count) : 0;
	const int player = order.left[pick];
	order.left[pick] = order.left[--order.count];
	return player;
}
