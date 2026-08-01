// [MGOOOOOO] Tests for the ripper budget arithmetic. The level-window cases are cross-checked
// against UZDoom's CheckRipLevel; everything else is ZandroX's own budget state machine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "gtest/gtest.h"

#include <limits.h>

#include "features/ripper/computation/ripper_compute.h"

using namespace zx::ripper;

namespace {

// [MGOOOOOO] A 16.16 fixed value from a plain multiplier, so the tests read in real units.
RipFixed Factor(double f)
{
	return (RipFixed)(f * (double)RIP_FRACUNIT);
}

FRipLimits Limits(int maxDamage, int perVictimHits, int totalHits, bool explodeOnLimit)
{
	FRipLimits l;
	l.maxDamage = maxDamage;
	l.perVictimHits = perVictimHits;
	l.totalHits = totalHits;
	l.explodeOnLimit = explodeOnLimit;
	return l;
}

FRipProgress Progress(int damageDone, int hitsDone, int hitsOnVictim)
{
	FRipProgress p;
	p.damageDone = damageDone;
	p.hitsDone = hitsDone;
	p.hitsOnVictim = hitsOnVictim;
	return p;
}

} // namespace

// ---- tiered ripping -------------------------------------------------------

TEST(RipLevel, NoWindowAlwaysAllows)
{
	// [MGOOOOOO] 0/0 is the default on every actor: ripping is ungated, as it was before.
	EXPECT_TRUE(ComputeRipLevelAllows(0, 0, 0));
	EXPECT_TRUE(ComputeRipLevelAllows(7, 0, 0));
	EXPECT_TRUE(ComputeRipLevelAllows(-3, 0, 0));
}

TEST(RipLevel, MinimumOnly)
{
	EXPECT_FALSE(ComputeRipLevelAllows(1, 2, 0));
	EXPECT_TRUE(ComputeRipLevelAllows(2, 2, 0));	// the bound is inclusive
	EXPECT_TRUE(ComputeRipLevelAllows(9, 2, 0));
}

TEST(RipLevel, MaximumOnly)
{
	EXPECT_TRUE(ComputeRipLevelAllows(1, 0, 3));
	EXPECT_TRUE(ComputeRipLevelAllows(3, 0, 3));	// the bound is inclusive
	EXPECT_FALSE(ComputeRipLevelAllows(4, 0, 3));
}

TEST(RipLevel, BothBounds)
{
	EXPECT_FALSE(ComputeRipLevelAllows(1, 2, 4));
	EXPECT_TRUE(ComputeRipLevelAllows(2, 2, 4));
	EXPECT_TRUE(ComputeRipLevelAllows(3, 2, 4));
	EXPECT_TRUE(ComputeRipLevelAllows(4, 2, 4));
	EXPECT_FALSE(ComputeRipLevelAllows(5, 2, 4));
}

TEST(RipLevel, ImpossibleWindowRejectsEverything)
{
	// [MGOOOOOO] Min above max is authorable in DECORATE; it simply never lets anything rip.
	EXPECT_FALSE(ComputeRipLevelAllows(1, 5, 2));
	EXPECT_FALSE(ComputeRipLevelAllows(3, 5, 2));
	EXPECT_FALSE(ComputeRipLevelAllows(6, 5, 2));
}

// ---- outcome before the hit -----------------------------------------------

TEST(RipOutcome, UnlimitedByDefault)
{
	// [MGOOOOOO] The pre-feature ripper: no budgets, so it rips forever no matter what it has done.
	EXPECT_EQ(RIP_DAMAGE, ComputeRipOutcome(FRipLimits(), FRipProgress()));
	EXPECT_EQ(RIP_DAMAGE, ComputeRipOutcome(FRipLimits(), Progress(99999, 500, 500)));
}

TEST(RipOutcome, DamageBudgetAlwaysExplodes)
{
	// [MGOOOOOO] RipperMaxDamage forces the Death state whether or not +RIPEXPLODEONLIMIT is set.
	EXPECT_EQ(RIP_EXPLODE, ComputeRipOutcome(Limits(90, 0, 0, false), Progress(90, 0, 0)));
	EXPECT_EQ(RIP_EXPLODE, ComputeRipOutcome(Limits(90, 0, 0, true), Progress(91, 0, 0)));
	EXPECT_EQ(RIP_DAMAGE, ComputeRipOutcome(Limits(90, 0, 0, false), Progress(89, 0, 0)));
}

TEST(RipOutcome, PerVictimBudgetGhostsWithoutTheFlag)
{
	EXPECT_EQ(RIP_DAMAGE, ComputeRipOutcome(Limits(0, 2, 0, false), Progress(0, 50, 1)));
	EXPECT_EQ(RIP_INERT, ComputeRipOutcome(Limits(0, 2, 0, false), Progress(0, 50, 2)));
	EXPECT_EQ(RIP_INERT, ComputeRipOutcome(Limits(0, 2, 0, false), Progress(0, 50, 3)));
}

TEST(RipOutcome, PerVictimBudgetExplodesWithTheFlag)
{
	EXPECT_EQ(RIP_DAMAGE, ComputeRipOutcome(Limits(0, 2, 0, true), Progress(0, 0, 1)));
	EXPECT_EQ(RIP_EXPLODE, ComputeRipOutcome(Limits(0, 2, 0, true), Progress(0, 0, 2)));
}

TEST(RipOutcome, LifetimeBudgetGhostsWithoutTheFlag)
{
	EXPECT_EQ(RIP_DAMAGE, ComputeRipOutcome(Limits(0, 0, 6, false), Progress(0, 5, 0)));
	EXPECT_EQ(RIP_INERT, ComputeRipOutcome(Limits(0, 0, 6, false), Progress(0, 6, 0)));
}

TEST(RipOutcome, LifetimeBudgetExplodesWithTheFlag)
{
	EXPECT_EQ(RIP_DAMAGE, ComputeRipOutcome(Limits(0, 0, 6, true), Progress(0, 5, 0)));
	EXPECT_EQ(RIP_EXPLODE, ComputeRipOutcome(Limits(0, 0, 6, true), Progress(0, 6, 0)));
}

TEST(RipOutcome, DamageBudgetOutranksAnInertCountBudget)
{
	// [MGOOOOOO] Both spent, no flag: the count budget alone would ghost, but RipperMaxDamage
	// must still force the Death state.
	EXPECT_EQ(RIP_EXPLODE, ComputeRipOutcome(Limits(50, 2, 0, false), Progress(50, 0, 2)));
}

TEST(RipOutcome, FreshVictimStillRippableAfterAnotherIsSpent)
{
	// [MGOOOOOO] The per-victim budget is per-victim: a spent monster does not disarm the rest.
	EXPECT_EQ(RIP_DAMAGE, ComputeRipOutcome(Limits(0, 2, 0, false), Progress(0, 2, 0)));
}

// ---- outcome after the hit ------------------------------------------------

TEST(RipSpends, NothingSpentKeepsFlying)
{
	EXPECT_FALSE(ComputeRipSpendsProjectile(FRipLimits(), Progress(500, 500, 500)));
	EXPECT_FALSE(ComputeRipSpendsProjectile(Limits(90, 3, 6, true), Progress(89, 5, 2)));
}

TEST(RipSpends, DamageBudgetDetonatesWithoutTheFlag)
{
	// [MGOOOOOO] The bite that crosses RipperMaxDamage lands in full, then detonates.
	EXPECT_TRUE(ComputeRipSpendsProjectile(Limits(90, 0, 0, false), Progress(90, 0, 0)));
	EXPECT_TRUE(ComputeRipSpendsProjectile(Limits(90, 0, 0, false), Progress(140, 0, 0)));
}

TEST(RipSpends, CountBudgetsNeedTheFlag)
{
	// [MGOOOOOO] Without +RIPEXPLODEONLIMIT a spent count budget goes inert instead of detonating,
	// so the projectile flies on and ComputeRipOutcome ghosts it through later contacts.
	EXPECT_FALSE(ComputeRipSpendsProjectile(Limits(0, 3, 0, false), Progress(0, 3, 3)));
	EXPECT_FALSE(ComputeRipSpendsProjectile(Limits(0, 0, 6, false), Progress(0, 6, 1)));

	EXPECT_TRUE(ComputeRipSpendsProjectile(Limits(0, 3, 0, true), Progress(0, 3, 3)));
	EXPECT_TRUE(ComputeRipSpendsProjectile(Limits(0, 0, 6, true), Progress(0, 6, 1)));
}

// ---- falloff --------------------------------------------------------------

TEST(RipDamageScale, FirstHitIsNeverScaled)
{
	EXPECT_EQ(10, ComputeScaledRipDamage(10, Factor(0.5), 0));
	EXPECT_EQ(10, ComputeScaledRipDamage(10, Factor(0.5), -1));
}

TEST(RipDamageScale, UnityFactorIsTheFastPath)
{
	EXPECT_EQ(10, ComputeScaledRipDamage(10, RIP_FRACUNIT, 5));
	EXPECT_EQ(10, ComputeScaledRipDamage(10, RIP_FRACUNIT, 1000000));
}

TEST(RipDamageScale, NonPositiveBaseIsPassedThrough)
{
	EXPECT_EQ(0, ComputeScaledRipDamage(0, Factor(0.5), 3));
	EXPECT_EQ(-4, ComputeScaledRipDamage(-4, Factor(0.5), 3));
}

TEST(RipDamageScale, NonPositiveFactorZeroesTheHit)
{
	EXPECT_EQ(0, ComputeScaledRipDamage(10, 0, 1));
	EXPECT_EQ(0, ComputeScaledRipDamage(10, Factor(-2.0), 1));
}

TEST(RipDamageScale, HalvingCompoundsAndTruncates)
{
	// [MGOOOOOO] RipperDamageFactor 0.5 on Damage 10: 10, 5, 2, 1, 0, 0...
	EXPECT_EQ(5, ComputeScaledRipDamage(10, Factor(0.5), 1));
	EXPECT_EQ(2, ComputeScaledRipDamage(10, Factor(0.5), 2));
	EXPECT_EQ(1, ComputeScaledRipDamage(10, Factor(0.5), 3));
	EXPECT_EQ(0, ComputeScaledRipDamage(10, Factor(0.5), 4));
	EXPECT_EQ(0, ComputeScaledRipDamage(10, Factor(0.5), 5));
}

TEST(RipDamageScale, RampUpCompounds)
{
	EXPECT_EQ(15, ComputeScaledRipDamage(10, Factor(1.5), 1));
	EXPECT_EQ(22, ComputeScaledRipDamage(10, Factor(1.5), 2));
}

TEST(RipDamageScale, DecayReachesExactlyZeroAndStops)
{
	// [MGOOOOOO] A tiny factor drives the accumulator to 0 in fixed point, which short-circuits
	// the remaining iterations rather than looping a million times.
	EXPECT_EQ(0, ComputeScaledRipDamage(10, Factor(0.001), 1000000));
}

TEST(RipDamageScale, SaturatesInsteadOfOverflowing)
{
	// [MGOOOOOO] Doubling from 1 crosses RIP_DAMAGE_CAP after 22 steps; a huge exponent must
	// clamp rather than wrap the 64-bit accumulator.
	EXPECT_EQ(RIP_DAMAGE_CAP, ComputeScaledRipDamage(1, Factor(2.0), 22));
	EXPECT_EQ(RIP_DAMAGE_CAP, ComputeScaledRipDamage(1, Factor(2.0), 1000000));
	EXPECT_EQ((1 << 21), ComputeScaledRipDamage(1, Factor(2.0), 21));
}

TEST(RipDamageScale, AbsurdFactorIsClampedNotOverflowed)
{
	// [MGOOOOOO] RipperDamageFactor is clamped to RIP_FACTOR_CAP before compounding, so even a
	// wildly out-of-range authored value stays inside signed 64-bit range.
	EXPECT_EQ(RIP_DAMAGE_CAP, ComputeScaledRipDamage(1, (RipFixed)INT_MAX * RIP_FRACUNIT, 4));
	EXPECT_EQ(RIP_DAMAGE_CAP, ComputeScaledRipDamage(1000, Factor(1000000.0), 3));
}

TEST(RipDamageScale, HugeBaseWithDecayStaysBounded)
{
	// [MGOOOOOO] An enormous base is clamped to the cap before the first multiply, so the
	// result is the cap scaled by the factor rather than an overflowed value.
	EXPECT_EQ(RIP_DAMAGE_CAP / 2, ComputeScaledRipDamage(INT_MAX, Factor(0.5), 1));
}

// ---- ledger gating --------------------------------------------------------

TEST(RipLedger, NotNeededForPlainRippers)
{
	// [MGOOOOOO] The pre-feature ripper pays nothing: no ledger, no allocation.
	EXPECT_FALSE(ComputeNeedsVictimLedger(FRipLimits(), RIP_FRACUNIT));
}

TEST(RipLedger, NotNeededForLifetimeOnlyBudgets)
{
	// [MGOOOOOO] RipperMaxCount and RipperMaxDamage are plain counters on the projectile.
	EXPECT_FALSE(ComputeNeedsVictimLedger(Limits(90, 0, 6, true), RIP_FRACUNIT));
}

TEST(RipLedger, NeededForPerVictimCount)
{
	EXPECT_TRUE(ComputeNeedsVictimLedger(Limits(0, 1, 0, false), RIP_FRACUNIT));
}

TEST(RipLedger, NeededForFalloff)
{
	EXPECT_TRUE(ComputeNeedsVictimLedger(FRipLimits(), Factor(0.5)));
	EXPECT_TRUE(ComputeNeedsVictimLedger(FRipLimits(), Factor(1.5)));
}
