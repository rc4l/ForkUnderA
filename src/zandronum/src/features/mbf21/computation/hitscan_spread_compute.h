// [rc4l] MBF21 hitscan-spread math for the parameterized bullet attacks (A_MonsterBulletAttack,
// A_WeaponBulletAttack). Pure so the fragile bits -- the triangular distribution, the int64 scaling
// that avoids overflow, and the signed/unsigned angle edges -- are unit-testable off-engine and
// pinned against the reference. Matches DSDA-Doom m_random.c P_RandomHitscanAngle / P_RandomHitscanSlope.
// The engine supplies the two RNG rolls (kept in the engine so demos/netcode stay deterministic) and
// the FixedToAngle(spread); this file does the arithmetic.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_MBF21_HITSCAN_SPREAD_COMPUTE_H
#define ZX_MBF21_HITSCAN_SPREAD_COMPUTE_H

#include <cstdint>

namespace zx { namespace mbf21 {

// Triangular hitscan spread. `spreadBam` is the max spread as a BAM angle (engine:
// FixedToAngle(abs(spread_in_fixed_degrees))); `rnd1`/`rnd2` are two independent 0..255 engine
// randoms. Returns the signed BAM angle offset:  spreadBam * (rnd1 - rnd2) / 255.
// int64 intermediate avoids the overflow of a 2^32-scale BAM times +/-255.
int ComputeHitscanAngleBAM(int64_t spreadBam, int rnd1, int rnd2);

// Vertical spread -> finetangent[] index (P_RandomHitscanSlope). Given the BAM angle offset, returns
// the index the engine should read from finetangent[] (0..FINEANGLES/2-1), with the two extreme
// clamps. Uses the standard Doom angle constants; done in unsigned to stay UBSan-clean while matching
// the reference for every reachable spread (a >90-degree spread is not expressible in real content).
int ComputeHitscanSlopeIndex(int angleOffsetBam);

}} // namespace zx::mbf21

#endif // ZX_MBF21_HITSCAN_SPREAD_COMPUTE_H
