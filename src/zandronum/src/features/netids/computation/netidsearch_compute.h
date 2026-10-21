// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The network ids IDList hands out, free ones marked in a bitmap so the next is found a word at a time rather than an entry at a time.

#ifndef ZX_NETIDSEARCH_COMPUTE_H
#define ZX_NETIDSEARCH_COMPUTE_H

#include <cstdint>

namespace zx { namespace netids {

const unsigned kIdCount = 65536;           // [rc4l] every unsigned short, 0 never handed out
const unsigned kIdWords = kIdCount / 64;   // [rc4l] the bitmap's words, a bit an id

// [rc4l] Id marked free or taken in bits.
void ComputeMarkId(uint64_t *bits, unsigned id, bool free);

// [rc4l] Whether id is marked free in bits.
bool ComputeIdFree(const uint64_t *bits, unsigned id);

// [rc4l] The first free id after from, up to 65535 and then from 1 up to from, as IDList::getNewID's walk finds it; from itself when no other id is free.
unsigned ComputeNextFreeId(const uint64_t *bits, unsigned from);

}} // namespace zx::netids

#endif // ZX_NETIDSEARCH_COMPUTE_H
