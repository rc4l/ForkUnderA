// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/relink-in-place/computation/relink_compute.h"

bool ComputeRelinkInPlace(const ZxRelinkAsk &ask, const ZxLinkedAt &last, bool mapHasPolyobjects)
{
	if (ask.movesXY || ask.inBlockmap || !ask.linked || mapHasPolyobjects) return false;
	return last.byPoint && last.noSector == ask.noSector && last.x == ask.x && last.y == ask.y && last.radius == ask.radius;
}

bool ComputeLinesCanStraddle(int64_t radius)
{
	return radius != 0;
}

bool ComputeJoinsSectorLists(bool noInteraction, bool clientSideOnly, bool asksForSectors)
{
	return !(noInteraction && clientSideOnly) || asksForSectors;
}
