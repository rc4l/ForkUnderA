// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure rules for relinks whose outcome is already known: an actor relinked where it was last linked, and a sector list that no line can add to.
#ifndef ZX_RELINK_COMPUTE_H
#define ZX_RELINK_COMPUTE_H

#include <cstdint>

// [Doruk-Aksoy] Where and how an actor was last linked: its x, y and radius, whether its sector came from that point and it was kept out of sector lists, and the sector and subsector that link set.
struct ZxLinkedAt
{
	int64_t x, y, radius;
	bool byPoint;
	bool noSector;
	void *sector;
	void *subsector;
};

// [Doruk-Aksoy] What a NOINTERACTION tick's relink would start from: whether it moves in x or y, is in the blockmap, is linked now, is kept out of sector lists, and its x, y and radius.
struct ZxRelinkAsk
{
	bool movesXY;
	bool inBlockmap;
	bool linked;
	bool noSector;
	int64_t x, y, radius;
};

// [Doruk-Aksoy] Whether unlinking and linking again would rebuild exactly what the last link built, so moving the actor to its sector list's head does the same: it stays in x and y, out of the blockmap, linked by its point where it was with the radius and sector-list flag it had, on a map without polyobjects, whose walls are the only ones that move.
bool ComputeRelinkInPlace(const ZxRelinkAsk &ask, const ZxLinkedAt &last, bool mapHasPolyobjects);

// [Doruk-Aksoy] Whether a sector list build must walk the lines near the actor: a radius-0 box has one corner, so no line can straddle it and only the actor's own sector is listed.
bool ComputeSecNodesWalkLines(int64_t radius);

#endif
