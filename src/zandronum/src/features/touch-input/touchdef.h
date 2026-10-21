// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The FUATOUCH lumps read into the layout the overlay draws and reads: every lump in load order, a later button replacing the one with its id.

#ifndef ZX_TOUCHDEF_H
#define ZX_TOUCHDEF_H

#include "features/touch-input/computation/touchdef_compute.h"

namespace zx { namespace touch {

// [rc4l] Reads every FUATOUCH lump, after the default bindings are set, since a button's command may take a spare joystick key's binding.
void LoadDefs();

// [rc4l] The layout the lumps gave, the default before LoadDefs.
const TouchLayout &Layout();

}} // namespace zx::touch

#endif // ZX_TOUCHDEF_H
