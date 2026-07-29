// [rc4l] The pitch->velocity decision shared by A_SpawnProjectile and A_CustomMissile.
//
// These two DECORATE actions are the same attack with ONE deliberate difference. A_CustomMissile
// (ZDoom-era, ours) computes the missile's vertical velocity with an inverted base pitch: it ADDS
// the missile's own velocity pitch when offsetting, and drives velz with +sin. Upstream fixed that
// in A_SpawnProjectile, and kept the broken arithmetic reachable behind CMF_BADPITCH so the decade
// of mods built against the old behaviour keep aiming where they always did. Upstream's own words
// for the flag's branch: "Replicate the bogus calculation from A_CustomMissile in its entirety.
// This tried to do the right thing but in the process effectively inverted the base pitch."
//
// So A_SpawnProjectile is NOT an alias for our A_CustomMissile -- aliasing it would silently give
// every mod written against the fixed function the inverted aim. A_CustomMissile therefore forces
// CMF_BADPITCH on, A_SpawnProjectile leaves it off, and both share this unit.
//
// SIGN CONVENTIONS (checked against upstream, not assumed). Upstream is float and defines
// TVector3::Pitch() as `-VecToAngle(XY().Length(), Z)` (vectors.h:1563) -- NEGATED relative to our
// `R_PointToAngle2(0, 0, xyLength, velz)`. Writing A for our unnegated angle, upstream's
//   bad:  Pitch -= Vel.Pitch();  Vel.Z =  Pitch.Sin() * Speed
//   good: Pitch += Vel.Pitch();  Vel.Z = -Pitch.Sin() * Speed
// become, in our convention:
//   bad:  pitch = pitch + A;     velz = +FixedMul(sin, Speed)   <- what A_CustomMissile always did
//   good: pitch = pitch - A;     velz = -FixedMul(sin, Speed)
//
// Ported from uzdoom@81fd6c819fd5a6b71a946ba6e95cb67a76e4cac7 (A_SpawnProjectile,
// src/playsim/p_actionfunctions.cpp): the arithmetic is upstream's, back-translated from their
// float/DAngle form into our fixed-point/angle_t form.
// The upstream file is VM-tainted, so this is a hand translation rather than a cherry-pick, per the
// VM-insulation policy in docs/, which forbids cherry-picking post-2016 thingdef code.
//
// Header-pure by the features/ rule: raw int64 fixed bits and uint32 angle bits only, no engine
// headers. The caller does the finesine/finecosine lookups and the fixed_t conversions.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SPAWNPROJECTILE_COMPUTE_H
#define ZX_SPAWNPROJECTILE_COMPUTE_H

#include <cstdint>

namespace zx
{

// [rc4l] Mirrors the CMF_* flags in thingdef_codeptr.cpp; static_asserted at the call site so the
// two definitions can never drift apart. CMF_BADPITCH's value (256) is upstream's.
enum
{
	ZX_CMF_ABSOLUTEPITCH = 16,
	ZX_CMF_OFFSETPITCH   = 32,
	ZX_CMF_BADPITCH      = 256,
};

// [rc4l] Does the pitch parameter drive the missile's vertical velocity at all? Only when the
// caller asked for an absolute or an offset pitch; otherwise the missile keeps the velocity
// P_SpawnMissile* already gave it and the pitch parameter is merely recorded by CMF_SAVEPITCH.
bool SpawnProjectileUsesPitch(int flags);

// [rc4l] The pitch actually used, in angle_t bits. Without CMF_OFFSETPITCH the parameter is used
// as given. With it, the missile's own velocity pitch is folded in -- and this is the inversion
// CMF_BADPITCH selects between. `missileVelPitch` is our unnegated
// R_PointToAngle2(0, 0, xyLength, velz). Wraparound is intentional: angle_t is modular.
uint32_t ComputeSpawnProjectilePitch(int flags, uint32_t pitch, uint32_t missileVelPitch);

// [rc4l] Resulting horizontal speed and vertical velocity, as raw fixed_t bits.
struct SpawnProjectileVelocity
{
	int64_t speedXY; // magnitude the caller then splits across x/y by the missile's yaw
	int64_t velZ;
};

// [rc4l] Velocity from the sine/cosine of the ALREADY-adjusted pitch (i.e. the output of
// ComputeSpawnProjectilePitch) and the missile's Speed property, all in raw fixed bits.
// speedXY is |cos * Speed| -- absolute, so a pitch past vertical cannot flip the missile's
// horizontal direction. velZ carries the sign that CMF_BADPITCH selects.
SpawnProjectileVelocity ComputeSpawnProjectileVelocity(int flags, int64_t sinPitch,
                                                       int64_t cosPitch, int64_t missileSpeed);

} // namespace zx

#endif // ZX_SPAWNPROJECTILE_COMPUTE_H
