// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACTORFLAGS_H
#define ZX_ACTORFLAGS_H

class AActor;

namespace zx { namespace actorflags {

// [Doruk-Aksoy] Sets or clears the flag `name` ("Class.FLAG" too) on `actor`, as DECORATE's A_ChangeFlag and ACS's SetActorFlag do: NOBLOCKMAP
// and NOSECTOR relinking, the map's kill, item and secret totals kept, and a server telling its clients; `localOnly`, for a clientside script,
// changes this machine's copy alone. False when the actor has no such flag, which `printError` reports.
bool ChangeActorFlag(AActor *actor, const char *name, bool on, bool printError, bool localOnly);

}} // namespace zx::actorflags

#endif
