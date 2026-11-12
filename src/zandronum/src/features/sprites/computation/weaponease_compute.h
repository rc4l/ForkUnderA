// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] cl_interpolateweapons (zandronum@18867e1e1300c5d19941336a7948595879dc1e3a): a bobbing weapon layer drawn between where it stood last tic and where it stands now.

#ifndef ZX_WEAPONEASE_COMPUTE_H
#define ZX_WEAPONEASE_COMPUTE_H

#include <stdint.h>

namespace zx { namespace sprites {

// [rc4l] One layer's last two tic positions as a renderer drew them, in raw 48.16 units; zero-initialise it.
struct WeaponEase
{
	bool seen;
	int tic;
	const void *owner;   // [rc4l] the body the layers were set up on, so a respawn starts afresh (zandronum@6b9fb57464d4a66b65540f1f4bb13a75f5317244)
	int64_t oldX, oldY, nowX, nowY;
};

// [rc4l] The layer's position eased from last tic's to this tic's by ticFrac, stepping the state when the tic moved on; off, first seen, on a new body or after a jump back or of over a second, it is drawn where it is.
void ComputeWeaponEase(WeaponEase &ease, bool enabled, int tic, const void *owner, int64_t x, int64_t y, int64_t ticFrac, int64_t *outX, int64_t *outY);

}} // namespace zx::sprites

#endif // ZX_WEAPONEASE_COMPUTE_H
