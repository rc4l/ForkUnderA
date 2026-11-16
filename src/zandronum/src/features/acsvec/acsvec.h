// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] fua_Vec, the vector operations zt-bcc's vec2 and vec3 compile to on this engine.

#ifndef ZX_ACSVEC_H
#define ZX_ACSVEC_H

#include "features/acsvec/computation/acsvec_compute.h"

class FBehavior;

namespace zx { namespace acsvec {

// [Doruk-Aksoy] Runs one fua_Vec call for the module whose code made it, returning the first result and writing the rest to cells 0 onward of its channel array.
int CallVecFunction(FBehavior* module, const int32_t* args, int argCount);

}} // namespace zx::acsvec

#endif
