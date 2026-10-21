// [MobiusU9B] Tests for the damage-taken computations, covering every branch of damagetaken_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/damage-taken/computation/damagetaken_compute.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using zx::damagetaken::ComputeDamageTakenAddRoll;
using zx::damagetaken::ComputeDamageTakenAttackerLabel;
using zx::damagetaken::ComputeDamageTakenCountedDamage;
using zx::damagetaken::ComputeDamageTakenIsRoll;
using zx::damagetaken::ComputeDamageTakenLines;
using zx::damagetaken::ComputeDamageTakenTimeText;
using zx::damagetaken::ComputeDamageTakenTypeLabel;
using zx::damagetaken::ComputeDamageTakenWeaponSource;
using zx::damagetaken::DamageTakenRoll;
using zx::damagetaken::DamageTakenStrike;
using zx::damagetaken::DamageTakenWeaponSource;

namespace
{
	const int kTicRate = 35;

	// [MobiusU9B] A non-fatal, non-telefrag roll with the given amounts and labels.
	DamageTakenRoll Roll(int damage, int armor, int health, const std::string &attacker = "ShotgunGuy",
		const std::string &weapon = "", const std::string &type = "Normal")
	{
		DamageTakenRoll roll;
		roll.damage = damage;
		roll.armorAbsorbed = armor;
		roll.healthLost = health;
		roll.telefrag = false;
		roll.lethal = false;
		roll.attacker = attacker;
		roll.weapon = weapon;
		roll.type = type;
		return roll;
	}

	// [MobiusU9B] A strike built the way the engine builds one, every roll on MAP01 at the same tic.
	DamageTakenStrike Strike(const std::vector<DamageTakenRoll> &rolls, int mapTics = 1497)
	{
		DamageTakenStrike strike = { "", -1, {} };
		for (const DamageTakenRoll &roll : rolls)
			ComputeDamageTakenAddRoll(strike, roll, "MAP01", mapTics);
		return strike;
	}
}

TEST(ComputeDamageTakenIsRoll, NeedsAnInflictor)
{
	EXPECT_FALSE(ComputeDamageTakenIsRoll(false, 5, 10));
}

TEST(ComputeDamageTakenIsRoll, CountsHealthLoss)
{
	EXPECT_TRUE(ComputeDamageTakenIsRoll(true, 0, 10));
}

TEST(ComputeDamageTakenIsRoll, CountsAHitArmorFullyAbsorbed)
{
	EXPECT_TRUE(ComputeDamageTakenIsRoll(true, 7, 0));
}

TEST(ComputeDamageTakenIsRoll, IgnoresAHitThatCostNothing)
{
	EXPECT_FALSE(ComputeDamageTakenIsRoll(true, 0, 0));
}

TEST(ComputeDamageTakenIsRoll, IgnoresHealingDamage)
{
	EXPECT_FALSE(ComputeDamageTakenIsRoll(true, 0, -10));
}

TEST(ComputeDamageTakenAddRoll, StartsAnEmptyStrikeAtTheRollsMapAndTic)
{
	DamageTakenStrike strike = { "", -1, {} };
	ComputeDamageTakenAddRoll(strike, Roll(10, 0, 10), "MAP01", 100);
	EXPECT_EQ("MAP01", strike.map);
	EXPECT_EQ(100, strike.mapTics);
	ASSERT_EQ(1u, strike.rolls.size());
	EXPECT_EQ(10, strike.rolls[0].damage);
}

TEST(ComputeDamageTakenAddRoll, AppendsRollsFromTheSameTic)
{
	DamageTakenStrike strike = { "", -1, {} };
	ComputeDamageTakenAddRoll(strike, Roll(10, 0, 10), "MAP01", 100);
	ComputeDamageTakenAddRoll(strike, Roll(5, 0, 5), "MAP01", 100);
	ASSERT_EQ(2u, strike.rolls.size());
	EXPECT_EQ(5, strike.rolls[1].damage);
}

TEST(ComputeDamageTakenAddRoll, ALaterTicReplacesTheStrike)
{
	DamageTakenStrike strike = { "", -1, {} };
	ComputeDamageTakenAddRoll(strike, Roll(10, 0, 10), "MAP01", 100);
	ComputeDamageTakenAddRoll(strike, Roll(10, 0, 10), "MAP01", 100);
	ComputeDamageTakenAddRoll(strike, Roll(3, 0, 3), "MAP01", 101);
	EXPECT_EQ(101, strike.mapTics);
	ASSERT_EQ(1u, strike.rolls.size());
	EXPECT_EQ(3, strike.rolls[0].damage);
}

TEST(ComputeDamageTakenAddRoll, AnotherMapReplacesTheStrikeEvenAtTheSameTic)
{
	DamageTakenStrike strike = { "", -1, {} };
	ComputeDamageTakenAddRoll(strike, Roll(10, 0, 10), "MAP01", 100);
	ComputeDamageTakenAddRoll(strike, Roll(4, 0, 4), "MAP02", 100);
	EXPECT_EQ("MAP02", strike.map);
	ASSERT_EQ(1u, strike.rolls.size());
	EXPECT_EQ(4, strike.rolls[0].damage);
}

TEST(ComputeDamageTakenCountedDamage, IsTheRollsDamage)
{
	EXPECT_EQ(45, ComputeDamageTakenCountedDamage(Roll(45, 15, 30)));
}

TEST(ComputeDamageTakenCountedDamage, ATelefragCountsOnlyWhatItRemoved)
{
	DamageTakenRoll roll = Roll(1000000, 20, 100);
	roll.telefrag = true;
	EXPECT_EQ(120, ComputeDamageTakenCountedDamage(roll));
}

TEST(ComputeDamageTakenTimeText, StartsAtZero)
{
	EXPECT_EQ("0:00.0", ComputeDamageTakenTimeText(0, kTicRate));
}

TEST(ComputeDamageTakenTimeText, PadsSecondsBelowTen)
{
	EXPECT_EQ("1:05.0", ComputeDamageTakenTimeText(65 * kTicRate, kTicRate));
}

TEST(ComputeDamageTakenTimeText, TruncatesToTenths)
{
	// [MobiusU9B] 1497 tics is 42.77 s.
	EXPECT_EQ("0:42.7", ComputeDamageTakenTimeText(1497, kTicRate));
}

TEST(ComputeDamageTakenTimeText, ReadsZeroForANegativeTime)
{
	EXPECT_EQ("0:00.0", ComputeDamageTakenTimeText(-5, kTicRate));
}

TEST(ComputeDamageTakenTimeText, ReadsZeroForANonPositiveTicRate)
{
	EXPECT_EQ("0:00.0", ComputeDamageTakenTimeText(1497, 0));
}

TEST(ComputeDamageTakenAttackerLabel, NoAttackerIsUnknown)
{
	EXPECT_EQ("unknown", ComputeDamageTakenAttackerLabel(false, false, "", "DoomImp"));
}

TEST(ComputeDamageTakenAttackerLabel, SelfDamageIsYourself)
{
	EXPECT_EQ("yourself", ComputeDamageTakenAttackerLabel(true, true, "Player", "DoomPlayer"));
}

TEST(ComputeDamageTakenAttackerLabel, APlayerIsNamed)
{
	EXPECT_EQ("Bob", ComputeDamageTakenAttackerLabel(true, false, "Bob", "DoomPlayer"));
}

TEST(ComputeDamageTakenAttackerLabel, AMonsterUsesItsActorName)
{
	EXPECT_EQ("Imp", ComputeDamageTakenAttackerLabel(true, false, "", "Imp"));
}

TEST(ComputeDamageTakenAttackerLabel, AnActorWithoutANameIsUnknown)
{
	EXPECT_EQ("unknown", ComputeDamageTakenAttackerLabel(true, false, "", ""));
}

TEST(ComputeDamageTakenWeaponSource, NamesAProjectile)
{
	EXPECT_EQ(DamageTakenWeaponSource::Inflictor, ComputeDamageTakenWeaponSource(false, true, false, true));
}

TEST(ComputeDamageTakenWeaponSource, NamesAShootableThingSuchAsABarrel)
{
	EXPECT_EQ(DamageTakenWeaponSource::Inflictor, ComputeDamageTakenWeaponSource(false, false, true, true));
}

TEST(ComputeDamageTakenWeaponSource, NamesAPlayersWeaponOverTheirPuff)
{
	EXPECT_EQ(DamageTakenWeaponSource::AttackerWeapon, ComputeDamageTakenWeaponSource(false, false, false, true));
}

TEST(ComputeDamageTakenWeaponSource, NamesAPlayersWeaponWhenTheyHitDirectly)
{
	EXPECT_EQ(DamageTakenWeaponSource::AttackerWeapon, ComputeDamageTakenWeaponSource(true, true, true, true));
}

TEST(ComputeDamageTakenWeaponSource, NamesAMonstersPuff)
{
	EXPECT_EQ(DamageTakenWeaponSource::Inflictor, ComputeDamageTakenWeaponSource(false, false, false, false));
}

TEST(ComputeDamageTakenWeaponSource, NamesNothingForAMonsterHittingDirectly)
{
	EXPECT_EQ(DamageTakenWeaponSource::None, ComputeDamageTakenWeaponSource(true, false, false, false));
}

TEST(ComputeDamageTakenTypeLabel, EmptyIsNormal)
{
	EXPECT_EQ("Normal", ComputeDamageTakenTypeLabel(""));
}

TEST(ComputeDamageTakenTypeLabel, TheEnginesNoneIsNormal)
{
	EXPECT_EQ("Normal", ComputeDamageTakenTypeLabel("None"));
}

TEST(ComputeDamageTakenTypeLabel, KeepsANamedType)
{
	EXPECT_EQ("Fire", ComputeDamageTakenTypeLabel("Fire"));
}

TEST(ComputeDamageTakenLines, BeforeAnyStrikeSaysSo)
{
	const DamageTakenStrike strike = { "", -1, {} };
	const std::vector<std::string> lines = ComputeDamageTakenLines(strike, kTicRate);
	ASSERT_EQ(1u, lines.size());
	EXPECT_EQ("You haven't been struck yet.", lines[0]);
}

TEST(ComputeDamageTakenLines, OneRollReadsAsOneHit)
{
	const std::vector<std::string> lines = ComputeDamageTakenLines(Strike({ Roll(15, 0, 15, "ShotgunGuy", "BulletPuff") }), kTicRate);
	ASSERT_EQ(3u, lines.size());
	EXPECT_EQ("Last strike: 1 hit, 15 damage (0 armor, 15 health)", lines[0]);
	EXPECT_EQ("  from ShotgunGuy (BulletPuff) on MAP01 at 0:42.7", lines[1]);
	EXPECT_EQ("  #1   15 dmg  armor   0  health  15", lines[2]);
}

TEST(ComputeDamageTakenLines, AShotgunBlastSumsItsPelletsUnderOneHeader)
{
	const std::vector<std::string> lines = ComputeDamageTakenLines(
		Strike({ Roll(10, 3, 7, "Bob", "Shotgun"), Roll(5, 1, 4, "Bob", "Shotgun") }), kTicRate);
	ASSERT_EQ(4u, lines.size());
	EXPECT_EQ("Last strike: 2 hits, 15 damage (4 armor, 11 health)", lines[0]);
	EXPECT_EQ("  from Bob (Shotgun) on MAP01 at 0:42.7", lines[1]);
	EXPECT_EQ("  #1   10 dmg  armor   3  health   7", lines[2]);
	EXPECT_EQ("  #2    5 dmg  armor   1  health   4", lines[3]);
}

TEST(ComputeDamageTakenLines, AnAttackerWithoutAWeaponIsNamedAlone)
{
	const std::vector<std::string> lines = ComputeDamageTakenLines(Strike({ Roll(20, 0, 20, "Demon") }), kTicRate);
	EXPECT_EQ("  from Demon on MAP01 at 0:42.7", lines[1]);
}

TEST(ComputeDamageTakenLines, MixedAttackersAreNamedPerRoll)
{
	const std::vector<std::string> lines = ComputeDamageTakenLines(
		Strike({ Roll(20, 0, 20, "Imp", "DoomImpBall", "Fire"), Roll(10, 0, 10, "ShotgunGuy", "BulletPuff") }), kTicRate);
	ASSERT_EQ(4u, lines.size());
	EXPECT_EQ("  on MAP01 at 0:42.7", lines[1]);
	EXPECT_EQ("  #1   20 dmg  armor   0  health  20  Imp (DoomImpBall)  Fire", lines[2]);
	EXPECT_EQ("  #2   10 dmg  armor   0  health  10  ShotgunGuy (BulletPuff)", lines[3]);
}

TEST(ComputeDamageTakenLines, TheSameAttackerWithAnotherWeaponIsNamedPerRoll)
{
	const std::vector<std::string> lines = ComputeDamageTakenLines(
		Strike({ Roll(100, 0, 100, "Bob", "Rocket"), Roll(30, 0, 30, "Bob", "ExplosiveBarrel") }), kTicRate);
	EXPECT_EQ("  on MAP01 at 0:42.7", lines[1]);
	EXPECT_EQ("  #2   30 dmg  armor   0  health  30  Bob (ExplosiveBarrel)", lines[3]);
}

TEST(ComputeDamageTakenLines, AFatalRollMarksTheHeaderAndTheRoll)
{
	DamageTakenRoll fatal = Roll(1000, 0, 25, "Cyberdemon", "Rocket");
	fatal.lethal = true;
	const std::vector<std::string> lines = ComputeDamageTakenLines(Strike({ fatal }), kTicRate);
	EXPECT_EQ("Last strike: 1 hit, 1000 damage (0 armor, 25 health), FATAL", lines[0]);
	EXPECT_EQ("  #1  1000 dmg  armor   0  health  25  FATAL", lines[2]);
}

TEST(ComputeDamageTakenLines, ATelefragReadsAsSuch)
{
	DamageTakenRoll telefrag = Roll(1000000, 50, 100, "Bob", "", "Telefrag");
	telefrag.telefrag = true;
	telefrag.lethal = true;
	const std::vector<std::string> lines = ComputeDamageTakenLines(Strike({ telefrag }), kTicRate);
	EXPECT_EQ("Last strike: 1 hit, 150 damage (50 armor, 100 health), FATAL", lines[0]);
	EXPECT_EQ("  #1  telefrag  armor  50  health 100  Telefrag  FATAL", lines[2]);
}
