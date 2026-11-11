// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/weaponease_compute.h"

namespace zx { namespace sprites {

// [rc4l] A gap longer than this is a load, a demo skip or a stall, not a tic to ease across.
static const int kMaxEaseGap = 35;

void ComputeWeaponEase(WeaponEase &ease, bool enabled, int tic, int64_t x, int64_t y, int64_t ticFrac, int64_t *outX, int64_t *outY)
{
	if (!enabled || !ease.seen || tic < ease.tic || tic > ease.tic + kMaxEaseGap)
	{
		ease.seen = enabled;
		ease.tic = tic;
		ease.oldX = ease.nowX = x;
		ease.oldY = ease.nowY = y;
		*outX = x;
		*outY = y;
		return;
	}
	if (tic != ease.tic)
	{
		ease.oldX = ease.nowX;
		ease.oldY = ease.nowY;
		ease.tic = tic;
	}
	ease.nowX = x;
	ease.nowY = y;
	*outX = ease.oldX + (((ease.nowX - ease.oldX) * ticFrac) >> 16);
	*outY = ease.oldY + (((ease.nowY - ease.oldY) * ticFrac) >> 16);
}

}} // namespace zx::sprites
