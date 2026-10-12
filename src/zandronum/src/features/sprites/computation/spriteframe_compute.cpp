// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spriteframe_compute.h"

namespace zx { namespace sprites {

int ComputeSpriteRotation(unsigned int angle, bool symmetric, unsigned int flipMask, bool &mirror)
{
	const unsigned int half45 = 0x20000000u / 2;    // [rc4l] ANGLE_45 / 2
	const unsigned int sixteenth = 0x80000000u / 16; // [rc4l] ANGLE_180 / 16
	const int rot = symmetric ? (int)((angle + half45 * 9) >> 28)
	                          : (int)((angle + half45 * 9 - sixteenth) >> 28);
	mirror = (flipMask & (1u << rot)) != 0;
	return rot;
}

}} // namespace zx::sprites
