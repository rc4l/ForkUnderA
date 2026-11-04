// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Lists of sound channels by a key, as one sound's or one actor's, beside the list of every channel, so a walk asking about one key visits only its own.
#ifndef ZX_SOUNDINDEX_COMPUTE_H
#define ZX_SOUNDINDEX_COMPUTE_H

#include <cstdint>
#include <unordered_map>

// [Doruk-Aksoy] A channel's place in one key's list; all zero, as a zeroed channel's is, when it is in none.
struct ZxIndexLink
{
	ZxIndexLink *next;
	ZxIndexLink **prev;
	void *owner;
};

// [Doruk-Aksoy] The first link of each key's list, a key with an empty list removed.
typedef std::unordered_map<intptr_t, ZxIndexLink *> ZxIndexHeads;

// [Doruk-Aksoy] Puts link, standing for owner, at the head of key's list, as the channel list links a new channel; one already listed is left where it is.
void ComputeIndexAdd(ZxIndexHeads &heads, intptr_t key, ZxIndexLink *link, void *owner);

// [Doruk-Aksoy] Takes link out of the list it is in, and the key with it once its list is empty; one in no list is left alone.
void ComputeIndexRemove(ZxIndexHeads &heads, intptr_t key, ZxIndexLink *link);

// [Doruk-Aksoy] The head of key's list, or NULL when it has none.
ZxIndexLink *ComputeIndexFirst(const ZxIndexHeads &heads, intptr_t key);

#endif
