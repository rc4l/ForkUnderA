// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Actors drawn wider than their radius, a big model on a small actor, and the sectors under them, so a walk that reaches any of those sectors draws the actor even when it never reached the one its centre is in.

#ifndef ZX_MODELREACH_H
#define ZX_MODELREACH_H

struct sector_t;

namespace zx { namespace modelreach {

// [Doruk-Aksoy] Finds the copy's wide actors and the sectors under each, once per fill; the main thread calls it before any walk reads them.
void PrepareWide();

// [Doruk-Aksoy] The record indices of the copy's wide actors, in record order.
const int *WideRecords(int &count);

// [Doruk-Aksoy] The sectors under a record's footprint, ascending, none for a record that is not wide.
const int *FootprintSectors(int record, int &count);

// [Doruk-Aksoy] Marks the sectors a walk reached, which FootprintReached then asks about, until the next mark.
void MarkReached(const int *sectors, int count);
void MarkReached(sector_t *const *sectors, int count);

// [Doruk-Aksoy] Whether a wide record's footprint holds a sector the last mark holds; false for a record that is not wide.
bool FootprintReached(int record);

// [Doruk-Aksoy] Whether the last mark holds a sector.
bool Reached(const sector_t *sector);

// [Doruk-Aksoy] Hands `actor` each wide record whose own sector the last mark misses and whose footprint it holds, the actors a walk that offers each reached sector's actors left out.
void EachWideMissed(void (*actor)(int record));

}} // namespace zx::modelreach

#endif // ZX_MODELREACH_H
