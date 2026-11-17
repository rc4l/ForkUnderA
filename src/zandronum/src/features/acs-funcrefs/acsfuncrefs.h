// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSFUNCREFS_H
#define ZX_ACSFUNCREFS_H

#include <cstdint>

class FBehavior;

namespace zx { namespace acsfuncrefs {

// [Doruk-Aksoy] A module loaded from `lump`'s place among the ACS library lumps, or -1 for a map's own BEHAVIOR.
int LibraryOrdinalOf(int lump);

// [Doruk-Aksoy] What PCD_PUSHFUNCTION pushes for function `funcnum` of `module`: the stable reference to its home library's function, or the load-position one for a map's own.
int32_t PushFunctionRef(FBehavior* module, int funcnum);

// [Doruk-Aksoy] The loaded module a reference names, with its function index in `funcnum`, or nullptr when no loaded module holds it.
FBehavior* ResolveFunctionRef(int32_t ref, int& funcnum);

// [Doruk-Aksoy] The console line for a reference ResolveFunctionRef found no module for, called in `script`.
void PrintMissingFunction(int32_t ref, const char* script);

}} // namespace zx::acsfuncrefs

#endif
