// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSFIELDS_H
#define ZX_ACSFIELDS_H

#include <cstdint>

#include "features/acs-fields/computation/fields_compute.h"

class AActor;

namespace zx { namespace acsfields {

// [Doruk-Aksoy] fua_GetField: element `index` of the actor's field `name` as ACS reads it, 0 for an unknown field or index.
int32_t GetField(AActor *actor, const char *name, int index);

// [Doruk-Aksoy] fua_SetField: writes an int user_ variable or array element, false for anything else.
bool SetField(AActor *actor, const char *name, int index, int32_t value);

}} // namespace zx::acsfields

#endif
