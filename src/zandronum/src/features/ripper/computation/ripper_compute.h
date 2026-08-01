// [MGOOOOOO] Ripper budget arithmetic: the per-hit decision a ripping missile makes when it
// touches a shootable actor. Extracted pure so the whole state machine is unit-testable without
// linking the game -- the engine side (p_map.cpp PIT_CheckThing) only reads the actor fields,
// calls these, and applies the outcome.
//
// Three independent budgets can stop a ripper, all authored in DECORATE on the projectile:
//   RipperCount      -- rip hits allowed against any ONE victim
//   RipperMaxCount   -- rip hits allowed over the projectile's whole life
//   RipperMaxDamage  -- cumulative damage actually dealt by ripping
// Zero means "unlimited" on all three, so every pre-existing ripper resolves to RIP_DAMAGE
// forever and nothing about vanilla behaviour changes.
//
// Tiered ripping (RipperLevel / RipLevelMin / RipLevelMax) is ported from GZDoom/UZDoom's
// CheckRipLevel -- see ripper_compute.cpp for the provenance link.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_RIPPER_COMPUTE_H
#define ZX_RIPPER_COMPUTE_H

namespace zx { namespace ripper {

// 16.16 fixed point, matching the fractional layout of the engine's fixed_t. Declared locally
// (and 64-bit wide) so this header stays free of engine headers; the engine's fixed_t is itself
// 64-bit in this fork, so RipperDamageFactor converts without truncation.
typedef long long RipFixed;

enum
{
	RIP_FRACBITS = 16,
	RIP_FRACUNIT = 1 << RIP_FRACBITS,	// 1.0

	// Largest damage ComputeScaledRipDamage will ever return. Well above TELEFRAG_DAMAGE
	// (1000000), and low enough that the intermediate 16.16 product cannot overflow a
	// signed 64-bit value.
	RIP_DAMAGE_CAP = 1 << 22,			// 4194304

	// RipperDamageFactor is clamped to this before compounding, for the same overflow reason.
	RIP_FACTOR_CAP = 128 * RIP_FRACUNIT,

	// Upper bound on per-victim ledger entries carried by one projectile. A ripper realistically
	// touches a handful of actors; the cap only exists so a pathological flight path cannot grow
	// the array without limit. See features/ripper/README.md.
	RIP_MAX_VICTIMS = 128,
};

// Tiered ripping: can a projectile of `ripperLevel` rip a victim whose window is
// [victimLevelMin, victimLevelMax]? Either bound is disabled by 0. A false result makes the
// projectile behave exactly as if the victim had +DONTRIP.
bool ComputeRipLevelAllows(int ripperLevel, int victimLevelMin, int victimLevelMax);

// The projectile's DECORATE-authored budget. All-zero (the default) means unlimited.
struct FRipLimits
{
	int  maxDamage;			// RipperMaxDamage
	int  perVictimHits;		// RipperCount
	int  totalHits;			// RipperMaxCount
	bool explodeOnLimit;	// +RIPEXPLODEONLIMIT

	FRipLimits() : maxDamage(0), perVictimHits(0), totalHits(0), explodeOnLimit(false) {}
};

// What the projectile has spent so far. `hitsOnVictim` is the ledger entry for the specific
// victim being touched right now (0 when it has never been ripped by this projectile).
struct FRipProgress
{
	int damageDone;		// RipperDamageDone
	int hitsDone;		// RipperHitsDone
	int hitsOnVictim;

	FRipProgress() : damageDone(0), hitsDone(0), hitsOnVictim(0) {}
};

enum ERipOutcome
{
	RIP_DAMAGE,		// rip normally: blood, sound, poison, damage, push
	RIP_INERT,		// pass through untouched -- no damage, no blood, no sound
	RIP_EXPLODE,	// treat the victim as solid so the blocked-missile path detonates us
};

// The decision made BEFORE the hit lands.
//
// A spent RipperMaxDamage always explodes (that is the property's whole contract). A spent count
// budget explodes only with +RIPEXPLODEONLIMIT, and otherwise goes inert. In normal play a count
// budget is caught by ComputeRipSpendsProjectile on the hit that spends it, so RIP_EXPLODE from
// a count budget is reachable here only when the projectile survived being spent -- e.g. a
// savegame written before the flag was added, or DECORATE that lowered the limit mid-flight.
ERipOutcome ComputeRipOutcome(const FRipLimits &limits, const FRipProgress &progress);

// The decision made AFTER the hit landed and `progress` was updated to include it. True when the
// projectile has just spent a budget and must detonate on this very hit rather than waiting for
// its next contact -- "pierce N times, then boom".
bool ComputeRipSpendsProjectile(const FRipLimits &limits, const FRipProgress &progress);

// Damage for hit number (priorHitsOnVictim + 1) against one victim: baseDamage * factor^prior.
// Saturates at RIP_DAMAGE_CAP rather than overflowing, and floors at 0 once the falloff has
// decayed past a whole point of damage.
int ComputeScaledRipDamage(int baseDamage, RipFixed factor, int priorHitsOnVictim);

// True when the projectile needs a per-victim ledger at all. When false the engine skips the
// bookkeeping entirely, which is the case for every ripper authored before this feature existed.
bool ComputeNeedsVictimLedger(const FRipLimits &limits, RipFixed damageFactor);

}} // namespace zx::ripper

#endif // ZX_RIPPER_COMPUTE_H
