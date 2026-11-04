// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/sound-index/computation/soundindex_compute.h"

void ComputeIndexAdd(ZxIndexHeads &heads, intptr_t key, ZxIndexLink *link, void *owner)
{
	if (link->prev != nullptr) return;
	// [Doruk-Aksoy] A map's values keep their address as it grows, so a head's slot can be pointed at.
	ZxIndexLink **head = &heads[key];
	link->next = *head;
	if (link->next != nullptr) link->next->prev = &link->next;
	*head = link;
	link->prev = head;
	link->owner = owner;
}

void ComputeIndexRemove(ZxIndexHeads &heads, intptr_t key, ZxIndexLink *link)
{
	if (link->prev == nullptr) return;
	*link->prev = link->next;
	if (link->next != nullptr) link->next->prev = link->prev;
	link->next = nullptr;
	link->prev = nullptr;
	link->owner = nullptr;
	auto it = heads.find(key);
	if (it != heads.end() && it->second == nullptr) heads.erase(it);
}

ZxIndexLink *ComputeIndexFirst(const ZxIndexHeads &heads, intptr_t key)
{
	auto it = heads.find(key);
	return it == heads.end() ? nullptr : it->second;
}
