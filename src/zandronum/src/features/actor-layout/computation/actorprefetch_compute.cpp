// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actor-layout/computation/actorprefetch_compute.h"

size_t ComputeTickPrefetchBytes(bool server, size_t hotEnd, size_t serverEnd)
{
	return server ? serverEnd : hotEnd;
}
