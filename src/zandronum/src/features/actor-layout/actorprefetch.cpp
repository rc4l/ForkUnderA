// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actor-layout/actorprefetch.h"
#include "actor.h"
#include "cmdlib.h"
#include <cstddef>

namespace zx { namespace actorlayout {

size_t TickBytes()
{
	return myoffsetof(AActor, LastHeard) + sizeof(TObjPtr<AActor>);
}

size_t CopyBytes()
{
	return myoffsetof(AActor, BlockNode);
}

}} // namespace zx::actorlayout
