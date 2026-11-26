// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_DECORATEEXPR_H
#define ZX_DECORATEEXPR_H

#include "thingdef/thingdef.h"
#include "thingdef/thingdef_exp.h"

namespace zx { namespace decorateuzdoom {

// [Doruk-Aksoy] A call to one of UZDoom's DECORATE math builtins or actor queries that ours lacked, or NULL when the name is none of them.
FxExpression *CreateGlobalCall(FName name, FArgumentList *args, const FScriptPosition &pos);

}} // namespace zx::decorateuzdoom

#endif // ZX_DECORATEEXPR_H
