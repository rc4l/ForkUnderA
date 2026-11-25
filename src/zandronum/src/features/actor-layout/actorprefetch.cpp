// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actor-layout/actorprefetch.h"
#include "features/actor-layout/computation/actorprefetch_compute.h"
#include "actor.h"
#include "cmdlib.h"
#include "features/net-session/netrole.h"
#include <cstddef>

namespace zx { namespace actorlayout {

size_t TickBytes()
{
	return ComputeTickPrefetchBytes(zx::net::IsServer(), myoffsetof(AActor, lastNetXUpdateTic), myoffsetof(AActor, lastVelz) + sizeof(fixed_t));
}

size_t CopyBytes()
{
	return myoffsetof(AActor, BlockNode);
}

}} // namespace zx::actorlayout
