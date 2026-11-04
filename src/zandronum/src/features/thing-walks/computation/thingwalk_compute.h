// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure rules for when a move's position check can skip walking the things in its blocks.
#ifndef ZX_THINGWALK_COMPUTE_H
#define ZX_THINGWALK_COMPUTE_H

// [Doruk-Aksoy] Mirrors actor.h's MF2_THRUACTORS, static_asserted where the engine includes this.
enum { ZX_MF2_THRUACTORS = 0x00100000 };

// [Doruk-Aksoy] Whether a mover with these flags2 clips against no thing, as PIT_CheckThing passes every thing for it before anything it does has an effect.
bool ComputeMoverPassesThings(int flags2);

#endif
