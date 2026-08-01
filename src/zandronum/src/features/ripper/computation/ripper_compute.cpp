// [MGOOOOOO] Implementation of the ripper budget arithmetic. See ripper_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "ripper_compute.h"

namespace zx { namespace ripper {

// [MGOOOOOO] Ported from uzdoom@cc166593e86ec132f92f83283c651135d49686a3: CheckRipLevel gates a
// rip on the victim's [RipLevelMin, RipLevelMax] window against the projectile's RipperLevel,
// with 0 disabling either bound.
bool ComputeRipLevelAllows(int ripperLevel, int victimLevelMin, int victimLevelMax)
{
	if (victimLevelMin > 0 && ripperLevel < victimLevelMin) return false;
	if (victimLevelMax > 0 && ripperLevel > victimLevelMax) return false;
	return true;
}

// A budget of 0 is "unlimited", so every test is gated on the limit being positive first.
static bool DamageBudgetSpent(const FRipLimits &limits, const FRipProgress &progress)
{
	return limits.maxDamage > 0 && progress.damageDone >= limits.maxDamage;
}

static bool CountBudgetSpent(const FRipLimits &limits, const FRipProgress &progress)
{
	if (limits.perVictimHits > 0 && progress.hitsOnVictim >= limits.perVictimHits) return true;
	if (limits.totalHits > 0 && progress.hitsDone >= limits.totalHits) return true;
	return false;
}

ERipOutcome ComputeRipOutcome(const FRipLimits &limits, const FRipProgress &progress)
{
	// RipperMaxDamage is a hard stop on the projectile itself: reaching it forces the Death
	// state regardless of +RIPEXPLODEONLIMIT.
	if (DamageBudgetSpent(limits, progress)) return RIP_EXPLODE;

	if (CountBudgetSpent(limits, progress))
		return limits.explodeOnLimit ? RIP_EXPLODE : RIP_INERT;

	return RIP_DAMAGE;
}

bool ComputeRipSpendsProjectile(const FRipLimits &limits, const FRipProgress &progress)
{
	if (DamageBudgetSpent(limits, progress)) return true;
	return limits.explodeOnLimit && CountBudgetSpent(limits, progress);
}

int ComputeScaledRipDamage(int baseDamage, RipFixed factor, int priorHitsOnVictim)
{
	// The overwhelmingly common case: no falloff authored, or this is the victim's first hit.
	if (priorHitsOnVictim <= 0 || factor == RIP_FRACUNIT) return baseDamage;

	// Non-positive damage has no meaningful falloff to apply; leave it exactly as the caller
	// computed it so damage-less rippers keep whatever semantics they already had.
	if (baseDamage <= 0) return baseDamage;
	if (factor <= 0) return 0;

	const RipFixed maxScaled = (RipFixed)RIP_DAMAGE_CAP << RIP_FRACBITS;
	const RipFixed f = (factor > (RipFixed)RIP_FACTOR_CAP) ? (RipFixed)RIP_FACTOR_CAP : factor;

	RipFixed scaled = (RipFixed)baseDamage << RIP_FRACBITS;

	for (int i = 0; i < priorHitsOnVictim; ++i)
	{
		// Clamp before multiplying, not after: this is what bounds the product to
		// RIP_DAMAGE_CAP * RIP_FACTOR_CAP, comfortably inside signed 64-bit range.
		if (scaled > maxScaled) scaled = maxScaled;

		scaled = (scaled * f) >> RIP_FRACBITS;

		// Decayed below a whole point of damage -- it can never climb back with f < 1.0.
		if (scaled <= 0) return 0;

		// Saturated. With f > 1.0 the sequence only grows, so stop here instead of spinning
		// through a large priorHitsOnVictim.
		if (scaled >= maxScaled) return RIP_DAMAGE_CAP;
	}

	return (int)(scaled >> RIP_FRACBITS);
}

bool ComputeNeedsVictimLedger(const FRipLimits &limits, RipFixed damageFactor)
{
	return limits.perVictimHits > 0 || damageFactor != RIP_FRACUNIT;
}

}} // namespace zx::ripper
