// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "follow_compute.h"
#include <gtest/gtest.h>

using namespace zx::netsnapshot;

// [Doruk-Aksoy] Seen 2026-11-26 at legacy suppression 2: KillThing is suppressed, so a client's legacy copy of a killed monster stayed alive for its scripts.
TEST(SnapshotFollow, ACorpseOnTheWireKillsALiveCopyAndALiveOneRaisesADeadCopy)
{
	EXPECT_EQ(kSnapLifeDied, ComputeLegacyLife(false, true));
	EXPECT_EQ(kSnapLifeRaised, ComputeLegacyLife(true, false));
}

// [Doruk-Aksoy] Agreeing with the wire already, the copy is left alone.
TEST(SnapshotFollow, ACopyThatAgreesWithTheWireIsLeftAlone)
{
	EXPECT_EQ(kSnapLifeSame, ComputeLegacyLife(false, false));
	EXPECT_EQ(kSnapLifeSame, ComputeLegacyLife(true, true));
}

// [Doruk-Aksoy] A wounded monster's last health would read as alive, so a death takes it to 0; health already at or under 0 stays.
TEST(SnapshotFollow, ADeadCopyReadsNoHealthAndKeepsHealthAlreadyGone)
{
	EXPECT_EQ(0, ComputeLegacyDeathHealth(35));
	EXPECT_EQ(0, ComputeLegacyDeathHealth(0));
	EXPECT_EQ(-20, ComputeLegacyDeathHealth(-20));
}

// [Doruk-Aksoy] Seen in netbroadcast at legacy suppression 2: an exploded rocket's copy outlived the server's for good, since a client never explodes a server's missile itself.
TEST(SnapshotFollow, ACopyWhoseActorTheServerRemovedIsDestroyed)
{
	EXPECT_TRUE(ComputeLeaveDestroysLegacy(false));
}

// [Doruk-Aksoy] A pickup also leaves the wire, and the legacy pickup commands still need the item.
TEST(SnapshotFollow, InventoryIsLeftToThePickupCommands)
{
	EXPECT_FALSE(ComputeLeaveDestroysLegacy(true));
}

// [Doruk-Aksoy] A death sets the corpse bit and clears what dying clears, leaving every other bit as it was.
TEST(SnapshotFollow, DeathSetsTheCorpseBitAndClearsOnlyWhatDyingClears)
{
	const uint32_t corpse = 0x100000, shootable = 0x4, solid = 0x2, other = 0x8000;
	EXPECT_EQ(other | corpse, ComputeLegacyDeathFlags(other | shootable | solid, corpse, shootable | solid));
}

// [Doruk-Aksoy] A raise clears the corpse bit and restores the cleared bits as the class has them, so a class that was never solid stays so.
TEST(SnapshotFollow, ARaiseRestoresOnlyTheClearedBitsTheClassHas)
{
	const uint32_t corpse = 0x100000, shootable = 0x4, solid = 0x2, other = 0x8000;
	EXPECT_EQ(other | shootable | solid, ComputeLegacyRaisedFlags(other | corpse, shootable | solid | other, corpse, shootable | solid));
	EXPECT_EQ(other | shootable, ComputeLegacyRaisedFlags(other | corpse, shootable, corpse, shootable | solid));
}
