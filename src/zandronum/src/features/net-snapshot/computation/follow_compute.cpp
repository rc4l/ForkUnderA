// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "follow_compute.h"

namespace zx { namespace netsnapshot {

int ComputeLegacyLife(bool legacyCorpse, bool wireCorpse)
{
	if (wireCorpse == legacyCorpse) return kSnapLifeSame;
	return wireCorpse ? kSnapLifeDied : kSnapLifeRaised;
}

int ComputeLegacyDeathHealth(int health) { return (health > 0) ? 0 : health; }

bool ComputeLeaveDestroysLegacy(bool isInventory) { return !isInventory; }

bool ComputeMirrorMayCollide(bool legacyHidden) { return !legacyHidden; }

uint32_t ComputeLegacyDeathFlags(uint32_t flags, uint32_t corpse, uint32_t cleared) { return (flags | corpse) & ~cleared; }

uint32_t ComputeLegacyRaisedFlags(uint32_t flags, uint32_t classFlags, uint32_t corpse, uint32_t restored)
{
	return (flags & ~corpse & ~restored) | (classFlags & restored);
}

}} // namespace zx::netsnapshot
