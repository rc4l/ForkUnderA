// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACTORPREFETCH_COMPUTE_H
#define ZX_ACTORPREFETCH_COMPUTE_H

#include <cstddef>

// [Doruk-Aksoy] How much of the next thinker to fetch while one ticks: the hot groups, through the position-update block on a server.
size_t ComputeTickPrefetchBytes(bool server, size_t hotEnd, size_t serverEnd);

#endif
