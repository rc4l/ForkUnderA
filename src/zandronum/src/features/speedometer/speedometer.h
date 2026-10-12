// [MobiusU9B] On-screen speedometer: the displayed player's speed in map units per second, drawn in the chat font where the player puts it.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_SPEEDOMETER_H
#define ZX_SPEEDOMETER_H

#include "doomtype.h"

namespace zx { namespace speedometer {

// [MobiusU9B] Draws the readout for displayPlayer when cl_fua_speedometer is on; called from D_Display right after HUD_Render.
void Draw(ULONG displayPlayer);

} } // namespace zx::speedometer

#endif
