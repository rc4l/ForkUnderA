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

// Whether a DEHACKED patch should get DSDHacked's unlimited thing/frame/sprite/sound numbering, from
// its declared "Doom version" and "Patch format". DSDHacked is a Patch-format-6 feature: the spec
// recommends "Doom version = 2021", but real MBF21/DSDHacked wads don't always set it (Judgment ships
// "Doom version = 21", "Patch format = 6"), and dsda-doom -- the behavior reference -- grows its
// tables on demand for ANY format-6 patch. So: enabled when the patch format is 6, or the explicit
// MBF21 marker (Doom version 2021) is present regardless of format. Gating on 2021 alone (as stock
// GZDoom does) leaves format-6/version-21 wads with their high frames "out of range" -> missing
// sprites. Enabling it for a plain Boom format-6 patch is harmless: the lazy allocation only fires
// for indices past the static pools, which such patches never reference.
bool ComputeDsdHackedEnabled(int doomVersion, int patchFormat);

}} // namespace zx::mbf21

#endif // ZX_MBF21_DEHACKED_FIELDS_COMPUTE_H
