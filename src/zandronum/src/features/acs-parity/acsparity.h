// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSPARITY_H
#define ZX_ACSPARITY_H

#include <cstdint>

#include "features/acs-parity/computation/acsparity_compute.h"

class AActor;

namespace zx { namespace acsparity {

// [Doruk-Aksoy] CheckInventory, or GetMaxInventory when `max`: how much of `type` the actor carries, or the most it can.
int InventoryAmount(AActor* actor, const char* type, bool max);

// [Doruk-Aksoy] CheckProximity: whether `count` actors of `className` stand within `distance` (16.16) of the actor `ptr` selects from `self`, by CPXF_ `flags`.
int CheckProximity(AActor* self, const char* className, int32_t distance, int count, int flags, int ptr);

// [Doruk-Aksoy] GetLineX (axis 0) or GetLineY (axis 1) of the first line with `lineId`, as 16.16, or 0 when no line has it.
int32_t LineCoord(int lineId, int axis, int32_t along, int32_t offset);

// [Doruk-Aksoy] The `index`th actor with `tid`, or `defactor` for tid 0.
AActor* NthActorFromTID(int tid, int index, AActor* defactor);

// [Doruk-Aksoy] The actor with net id `id`, or null.
AActor* ActorFromNetID(int32_t id);

// [Doruk-Aksoy] The name of the terrain under `actor`'s feet.
const char* FloorTerrainName(AActor* actor);

// [Doruk-Aksoy] Whether `name` is an actor class.
bool IsActorClass(const char* name);

}} // namespace zx::acsparity

#endif
