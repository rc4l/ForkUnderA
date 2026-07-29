// [rc4l] Pure conversions from raw DeHackEd thing-field values to the stored engine values MBF21
// uses, extracted so the encoding (group offsets past sentinels, the meleerange convention shift) is
// unit-testable off-engine. The DEH parser (d_dehacked.cpp PatchThing) reads a value and calls these.
// Group encoding matches DSDA-Doom d_deh.c; meleerange follows Zandronum lz/mbf21's ZDoom convention.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_MBF21_DEHACKED_FIELDS_COMPUTE_H
#define ZX_MBF21_DEHACKED_FIELDS_COMPUTE_H

#include <cstdint>

#include "features/mbf21/computation/damage_groups_compute.h"

namespace zx { namespace mbf21 {

// DEH "Infighting group N" (N >= 0, the parser rejects negatives) -> stored value, offset past
// IG_DEFAULT so DEH group 0 is a real group distinct from "unset".
int ComputeInfightingGroupStored(int dehValue);

// DEH "Projectile group N" -> stored. A negative DEH value means PG_GROUPLESS (no immunity even to
// your own species); otherwise offset past the built-in PG_DEFAULT/PG_BARON slots.
int ComputeProjectileGroupStored(int dehValue);

// DEH "Splash group N" (N >= 0) -> stored value, offset past SG_DEFAULT.
int ComputeSplashGroupStored(int dehValue);

// DEH "Melee range" (16.16 fixed-point) -> the actor's meleerange. The DEH value uses the vanilla
// MELEERANGE convention (includes the target radius); ZDoom/ZandroX's meleerange does not, so drop a
// standard 20-unit radius. int64 because fixed_t is 64-bit in ZandroX. (Matches Zandronum lz/mbf21.)
int64_t ComputeMeleeRangeFixed(int64_t dehFixedValue);

}} // namespace zx::mbf21

#endif // ZX_MBF21_DEHACKED_FIELDS_COMPUTE_H
