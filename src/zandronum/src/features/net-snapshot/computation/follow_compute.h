// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Doruk-Aksoy
//
// Filename: follow_compute.h
//
// Description: How a client's hidden legacy copy of a mirrored actor follows
// its mirror's life below legacy suppression level 3, and the render words a
// mirror takes from that copy. Pure: ghostactor.cpp gathers the inputs.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_FOLLOW_COMPUTE_H__
#define __SNAP_FOLLOW_COMPUTE_H__

#include <cstdint>

namespace zx { namespace netsnapshot {

// [Doruk-Aksoy] What a legacy copy's life does this tic: nothing, die as its mirror did, or rise again as an Arch-vile's raise does.
enum SnapLegacyLife { kSnapLifeSame = 0, kSnapLifeDied, kSnapLifeRaised };

// [Doruk-Aksoy] The change from the copy's own corpse flag to the corpse bit the wire carries.
int ComputeLegacyLife(bool legacyCorpse, bool wireCorpse);

// [Doruk-Aksoy] The health a copy reads once dead: what it held if that was already none, else 0, since the death's own health came in the suppressed KillThing.
int ComputeLegacyDeathHealth(int health);

// [Doruk-Aksoy] A copy's flag word once dead: `corpse` set and `cleared` (what Die and A_NoBlocking clear) taken off.
uint32_t ComputeLegacyDeathFlags(uint32_t flags, uint32_t corpse, uint32_t cleared);

// [Doruk-Aksoy] Whether a copy whose ghost left, its actor removed on the server, is destroyed: anything but inventory, which a pickup also takes off the wire while the legacy pickup commands still handle it.
bool ComputeLeaveDestroysLegacy(bool isInventory);

// [Doruk-Aksoy] Whether a mirror may turn solid and enter the blockmap: only where no hidden legacy copy holds its id (level 3), since that copy follows it, blocks the player already, and the blockmap searches would find the actor twice.
bool ComputeMirrorMayCollide(bool legacyHidden);

// [Doruk-Aksoy] A copy's flag word once raised: `corpse` taken off and the `restored` bits put back as its class has them.
uint32_t ComputeLegacyRaisedFlags(uint32_t flags, uint32_t classFlags, uint32_t corpse, uint32_t restored);

// [Doruk-Aksoy] A mirror's render word once it takes its legacy copy's, which the flag commands still reach below level 3: every bit from the copy but those in kept, which the mirror holds as the snapshot drives them.
uint32_t ComputeMirrorRenderWord(uint32_t mirrorWord, uint32_t legacyWord, uint32_t kept);

}} // namespace zx::netsnapshot

#endif // __SNAP_FOLLOW_COMPUTE_H__
