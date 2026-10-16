// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The ids the render copy gives actors: a serial never reused, so a cache keyed on one cannot mistake a new actor at a freed actor's address for it.

#ifndef ZX_SERIAL_COMPUTE_H
#define ZX_SERIAL_COMPUTE_H

namespace zx { namespace gamecopy {

// [rc4l] No actor, which an actor not yet taken by a fill holds.
const unsigned int kNoId = 0;

// [rc4l] The id after `last`, never kNoId, so one that wraps past the top starts again at 1.
inline unsigned int ComputeNextSerial(unsigned int last)
{
	return last + 1 == kNoId ? 1 : last + 1;
}

}} // namespace zx::gamecopy

#endif // ZX_SERIAL_COMPUTE_H
